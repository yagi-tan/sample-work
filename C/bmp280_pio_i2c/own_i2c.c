#include "own_i2c.h"
#include "own_i2c.pio.h"
#include "utilities.h"

#include <hardware/dma.h>
#include <pico/time.h>

#include <stdlib.h>
#include <string.h>

#define DMA_BUF_SZ 16u												//in bytes

enum own_i2c_modes {
	OWN_I2C_WRITE = 0u,
	OWN_I2C_READ
};

static void setup_common_sm_config(pio_sm_config *c);				//forward declaration
static void start_operation();
static bool wait_operation_done();

static float i2c_clock_div = 0.0f;
static uint i2c_pin_scl = -1u, i2c_pin_sda = -1u;					//SCL = SDA + 1
static PIO i2c_pio = NULL;
static int i2c_sm = -1;

static uint8_t *dma_buf_rx = NULL, *dma_buf_tx = NULL;
static int dma_channel_rx = -1, dma_channel_tx = -1;
static uint8_t dma_counter;											//only for RX

static bool op_pending = false, sess_pending = false;

//! Helper function to cleanup an operation within a session.
//! @param[in] prog PIO program currently loaded.
//! @param[in] offset PIO instruction memory offset of program currently loaded.
static inline void cleanup_operation(const pio_program_t *prog, uint offset) {
	pio_sm_set_enabled(i2c_pio, i2c_sm, false);
	pio_remove_program(i2c_pio, prog, offset);
}

//! Helper function to begin I2C session (start signal, address + mode).
//! @param[in] dev_addr Target deivce address.
//! @param[in] mode I2C mode.
//! @return True if I2C session began successfully.
static bool i2c_begin_session(uint8_t dev_addr, enum own_i2c_modes mode) {
	bool result = false;
	
	if (pio_can_add_program(i2c_pio, &own_i2c_begin_program)) {
		const uint offset = pio_add_program(i2c_pio, &own_i2c_begin_program);
		pio_sm_config c = own_i2c_begin_program_get_default_config(offset);
		setup_common_sm_config(&c);
		sm_config_set_in_shift(&c, false, true, 1u);				//will contain target ACK status
		pio_sm_init(i2c_pio, i2c_sm, offset, &c);
		
		i2c_pio->txf[i2c_sm] = ((dev_addr << 1u) + mode) << 24u;	//7-bit address + mode bit
		
		start_operation();
		if (wait_operation_done()) {
			if (pio_sm_is_rx_fifo_empty(i2c_pio, i2c_sm)) {
				send_string("I2C begin operation done with status missing.\n");
			}
			else {
				io_ro_32 status = i2c_pio->rxf[i2c_sm];
				
				if (status) {
					send_string("I2C begin operation done with NACK status (%u).\n", status);
				}
				else {
					result = true;
				}
			}
		}
		cleanup_operation(&own_i2c_begin_program, offset);
	}
	else {
		send_string("Error adding PIO program for I2C begin.\n");
	}
	
	return result;
}

//! Helper function to end current I2C session (prepare bus for repeat \b START event or set \b STOP bit).
//! @param[in] keep_session True if current line control is to be kept instead of ending it (set \b STOP bit).
//! @return True if I2C bus got set to expected state.
static bool i2c_end_session(bool keep_session) {
	const pio_program_t *const prog = keep_session ? &own_i2c_keep_program : &own_i2c_stop_program;
	bool result = false;
	
	if (pio_can_add_program(i2c_pio, prog)) {
		const uint offset = pio_add_program(i2c_pio, prog);
		pio_sm_config c = keep_session ? own_i2c_keep_program_get_default_config(offset) :
			own_i2c_stop_program_get_default_config(offset);
		setup_common_sm_config(&c);
		pio_sm_init(i2c_pio, i2c_sm, offset, &c);
		
		start_operation();
		if (wait_operation_done()) {
			result = true;
		}
		cleanup_operation(prog, offset);
	}
	else {
		send_string("Error adding PIO program for I2C end (keep:%u).\n", keep_session);
	}
	
	return result;
}

//! Helper function to setup common state machine configs (clock, pins) for all PIO programs.
//! @param[out] c State machine config.
static void setup_common_sm_config(pio_sm_config *c) {
	sm_config_set_clkdiv(c, i2c_clock_div);
	sm_config_set_out_shift(c, false, true, 8u);
	sm_config_set_in_shift(c, false, true, 8u);
	sm_config_set_out_pins(c, i2c_pin_sda, 1);
	sm_config_set_set_pins(c, i2c_pin_sda, 1);
	sm_config_set_in_pins(c, i2c_pin_sda);
	sm_config_set_sideset_pins(c, i2c_pin_scl);
	sm_config_set_jmp_pin(c, i2c_pin_sda);
}

//! Helper function to start an operation within a session.
static inline void start_operation() {
	pio_interrupt_clear(i2c_pio, 0u);
	op_pending = true;
	pio_sm_set_enabled(i2c_pio, i2c_sm, true);
}

//! Waits for pending operation to finish. It's possible for operation to not finish in time.
//! @return True if operation finished in time, regardless of result.
static bool wait_operation_done() {
	const absolute_time_t t = make_timeout_time_ms(10u);			//wait for 10msec before giving up
	bool result;
	
	while (op_pending && !time_reached(t)) {
		tight_loop_contents();
	}
	
	if (op_pending) {
		send_string("I2C operation didn't complete in time.\n");
		result = false;
	}
	else {
		result = true;
	}
	
	return result;
}

//! \b DMA_IRQ_0 handler for I2C DMA RX transfer.
//! @param[in,out] ints Interrupt state for \b DMA_IRQ_0. Corresponding bit is to be cleared if handled.
inline void own_i2c_dma_irq0_handler(io_rw_32 *ints) {
	const uint32_t mask_rx = 1u << dma_channel_rx;
	
	if (*ints & mask_rx) {
		++dma_counter;
		*ints = mask_rx;
		dma_channel_set_write_addr(dma_channel_rx, dma_buf_rx + dma_counter, true);
	}
}

//! \b PIO0_IRQ_0 handler for state machine internal IRQ 0 (as I2C operation pending flag).
inline void own_i2c_pio0_irq0_handler() {
	if (pio_interrupt_get(i2c_pio, 0u)) {
		op_pending = false;
		pio_interrupt_clear(i2c_pio, 0u);
	}
}

//! PIO subsystem initializer.
//! @param[in] pio Target PIO instance.
//! @param[in] pin_base Base GPIO index for I2C 2-pins, in {SDA, SCL} order.
//! @param[in] bit_rate Expected data bit rate, in bit per second.
//! @return True if no error has occurred.
bool own_i2c_init(PIO pio, uint pin_base, uint32_t bit_rate) {
	bool result = true;
	
	i2c_clock_div = (SYS_CLK_KHZ * 1000u) / (float)(bit_rate * 8u);
	i2c_pin_scl = pin_base + 1u;
	i2c_pin_sda = pin_base + 0u;
	i2c_pio = pio;
	i2c_sm = pio_claim_unused_sm(pio, false);
	
	if (i2c_sm == -1) {
		send_string("No free state machine in target PIO for I2C.\n");
		result = false;
	}
	
	dma_buf_tx = calloc(DMA_BUF_SZ, sizeof(uint8_t));
	dma_channel_rx = dma_claim_unused_channel(false);
	dma_channel_tx = dma_claim_unused_channel(false);
	
	if (!dma_buf_tx) {
		send_string("Error allocating DMA target buffer(s) for I2C.\n");
		result = false;
	}
	if ((dma_channel_rx == -1) || (dma_channel_tx == -1)) {
		send_string("No free DMA channel(s) for I2C.\n");
		result = false;
	}
	
	if (result) {
		dma_channel_set_irq0_enabled(dma_channel_tx, false);
		
		//avoid glitching the bus when output mux switched from GPIO function to PIO. OE inversion is needed
		//to match output bit being sent, else it will get inverted if used as-is.
		//******************************************************************************
		pio_sm_config c = pio_get_default_sm_config();
		sm_config_set_set_pins(&c, i2c_pin_sda, 2u);
		pio_sm_set_config(pio, i2c_sm, &c);
		
		gpio_pull_up(i2c_pin_scl);
		gpio_pull_up(i2c_pin_sda);
		pio_sm_exec(pio, i2c_sm, pio_encode_set(pio_pindirs, 0x03u));	//OE=1 -> output enabled
		pio_sm_exec(pio, i2c_sm, pio_encode_set(pio_pins, 0x03u));	//set pins to 1
		pio_gpio_init(pio, i2c_pin_scl);
		gpio_set_oeover(i2c_pin_scl, GPIO_OVERRIDE_INVERT);			//OE=1 -> output disabled = hi-Z
		pio_gpio_init(pio, i2c_pin_sda);
		gpio_set_oeover(i2c_pin_sda, GPIO_OVERRIDE_INVERT);			//OE=1 -> output disabled = hi-Z
		pio_sm_exec(pio, i2c_sm, pio_encode_set(pio_pins, 0x00u));	//set pins to 0
		//******************************************************************************
		
		//map state machine internal IRQ to PIO IRQ as operation ended flag
		pio_interrupt_clear(pio, 0u);
		pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
	}
	
	return result;
}

//! Sends read command, blocking execution while operation is not finished.
//! @param[in] dev_addr Target device address.
//! @param[out] data Output buffer to put received data from target device.
//! @param[in] data_sz Expected received data amount, in bytes. Must be 0 &lt; x &lt; (\ref DMA_BUF_SZ * 8).
//! @param[in] keep_session True if current line control is to be kept instead of ending it (set \b STOP bit).
//! @return Received byte count, or -1 if error occurred during operation.
int own_i2c_read_blocking(uint8_t dev_addr, uint8_t *data, size_t data_sz, bool keep_session) {
	int result = -1;
	
	if (sess_pending) {
		send_string("Pending I2C session when trying to read.\n");
	}
	else if (!data_sz || (data_sz >= (DMA_BUF_SZ * 8u))) {
		send_string("Invalid I2C read size %zu.\n", data_sz);
	}
	else {
		sess_pending = true;
		
		if (!i2c_begin_session(dev_addr, OWN_I2C_READ)) {}
		else if (pio_can_add_program(i2c_pio, &own_i2c_read_program)) {
			const uint offset = pio_add_program(i2c_pio, &own_i2c_read_program);
			pio_sm_config c = own_i2c_read_program_get_default_config(offset);
			setup_common_sm_config(&c);
			pio_sm_init(i2c_pio, i2c_sm, offset, &c);
			
			i2c_pio->txf[i2c_sm] = (data_sz - 1u) << 24u;			//ACK+NACK bit count
			pio_sm_exec(i2c_pio, i2c_sm, pio_encode_out(pio_y, 8u));//scratch Y now hold the value
			
			//DMA setup
			//******************************************************************************
			dma_buf_rx = data;
			dma_counter = 0u;
			
			{
				//write buffer is for ACK/NACK reply to target byte(s)
				const size_t set_bits = data_sz - 1u, full_bytes = set_bits / 8u;	//NACK for last byte
				memset(dma_buf_tx, 0u, full_bytes);
				dma_buf_tx[full_bytes] = 0x01u << (7u - set_bits % 8u);
				
				dma_channel_config dma_cfg_rx = dma_channel_get_default_config(dma_channel_rx);
				channel_config_set_dreq(&dma_cfg_rx, pio_get_dreq(i2c_pio, i2c_sm, false));
				channel_config_set_transfer_data_size(&dma_cfg_rx, DMA_SIZE_8);
				channel_config_set_read_increment(&dma_cfg_rx, false);
				channel_config_set_write_increment(&dma_cfg_rx, true);
				dma_channel_configure(dma_channel_rx, &dma_cfg_rx, data, i2c_pio->rxf + i2c_sm, 1u, false);
				dma_channel_set_irq0_enabled(dma_channel_rx, true);
				
				dma_channel_config dma_cfg_tx = dma_channel_get_default_config(dma_channel_tx);
				channel_config_set_dreq(&dma_cfg_tx, pio_get_dreq(i2c_pio, i2c_sm, true));
				channel_config_set_transfer_data_size(&dma_cfg_tx, DMA_SIZE_8);
				channel_config_set_read_increment(&dma_cfg_tx, true);
				channel_config_set_write_increment(&dma_cfg_tx, false);
				dma_channel_configure(dma_channel_tx, &dma_cfg_tx, i2c_pio->txf + i2c_sm, dma_buf_tx,
					full_bytes + 1u, false);
			}
			//******************************************************************************
			
			dma_start_channel_mask((1u << dma_channel_rx) | (1u << dma_channel_tx));
			
			start_operation();
			if (wait_operation_done()) {
				if (dma_counter == data_sz) {
					result = data_sz;
				}
				else {
					send_string("I2C read operation data size mismatch (expect:%u got:%u).\n", data_sz,
						dma_counter);
				}
			}
			cleanup_operation(&own_i2c_read_program, offset);
			
			dma_channel_cleanup(dma_channel_rx);
			dma_channel_cleanup(dma_channel_tx);
			dma_buf_rx = NULL;
		}
		else {
			send_string("Error adding PIO program for I2C read.\n");
		}
		
		if (!i2c_end_session((result != -1) && keep_session)) {
			result = -1;
		}
		
		sess_pending = false;
	}
	
	return result;
}

//! Sends write command, blocking execution while operation is not finished.
//! @param[in] dev_addr Target device address.
//! @param[in] data Output buffer to put received data from target device.
//! @param[in] data_sz Expected received data amount, in bytes. Must be 0 &lt; x &lt; (\ref DMA_BUF_SZ * 8).
//! @param[in] keep_session True if current line control is to be kept instead of ending it (set \b STOP bit).
//! @return Sent byte count, or -1 if error occurred during operation.
int own_i2c_write_blocking(uint8_t dev_addr, const uint8_t *data, size_t data_sz, bool keep_session) {
	int result = -1;
	
	if (sess_pending) {
		send_string("Pending I2C session when trying to write.\n");
	}
	else if (!data_sz) {
		send_string("Invalid I2C write size %zu.\n", data_sz);
	}
	else {
		sess_pending = true;
		
		if (!i2c_begin_session(dev_addr, OWN_I2C_WRITE)) {}
		else if (pio_can_add_program(i2c_pio, &own_i2c_write_program)) {
			const uint offset = pio_add_program(i2c_pio, &own_i2c_write_program);
			pio_sm_config c = own_i2c_write_program_get_default_config(offset);
			setup_common_sm_config(&c);
			pio_sm_init(i2c_pio, i2c_sm, offset, &c);
			
			i2c_pio->txf[i2c_sm] = (data_sz - 1u) << 24u;			//data byte count
			pio_sm_exec(i2c_pio, i2c_sm, pio_encode_out(pio_y, 8u));//scratch Y now hold the value
			
			//DMA setup
			//**************************************************************************
			{
				dma_channel_config dma_cfg_tx = dma_channel_get_default_config(dma_channel_tx);
				channel_config_set_dreq(&dma_cfg_tx, pio_get_dreq(i2c_pio, i2c_sm, true));
				channel_config_set_transfer_data_size(&dma_cfg_tx, DMA_SIZE_8);
				channel_config_set_read_increment(&dma_cfg_tx, true);
				channel_config_set_write_increment(&dma_cfg_tx, false);
				dma_channel_configure(dma_channel_tx, &dma_cfg_tx, i2c_pio->txf + i2c_sm, data, data_sz,
					true);
			}
			//**************************************************************************
			
			start_operation();
			if (wait_operation_done()) {
				if (pio_sm_is_rx_fifo_empty(i2c_pio, i2c_sm)) {
					result = data_sz;
				}
				else {
					send_string("I2C write operation data got NACK from target.\n");
				}
			}
			cleanup_operation(&own_i2c_write_program, offset);
			
			dma_channel_cleanup(dma_channel_tx);
		}
		else {
			send_string("Error adding PIO program for I2C write.\n");
		}
		
		if (!i2c_end_session((result != -1) && keep_session)) {
			result = -1;
		}
		
		sess_pending = false;
	}
	
	return result;
}
