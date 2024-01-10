#include "own_spi.h"
#include "own_spi.pio.h"
#include "utilities.h"

#include <hardware/dma.h>
#include <pico/time.h>

#include <stdlib.h>
#include <string.h>

#define DMA_BUF_SZ 16u												//in bytes

static void setup_common_sm_config(pio_sm_config *c);				//forward declaration
static void start_operation();
static bool wait_operation_done();

static float spi_clock_div = 0.0f;
static uint spi_pin_csn = -1, spi_pin_rx = -1u, spi_pin_sck = -1, spi_pin_tx = -1u;	//{RX = 0, CSN, SCK, TX}
static PIO spi_pio = NULL;
static int spi_sm = -1;

static uint8_t *dma_buf_rx = NULL, *dma_buf_tx = NULL;
static int dma_channel_rx = -1, dma_channel_tx = -1;
static uint8_t dma_counter;											//only for RX

static bool op_pending = false, sess_pending = false;

//! Helper function to cleanup an operation within a session.
//! @param[in] prog PIO program currently loaded.
//! @param[in] offset PIO instruction memory offset of program currently loaded.
static inline void cleanup_operation(const pio_program_t *prog, uint offset) {
	pio_sm_set_enabled(spi_pio, spi_sm, false);
	pio_remove_program(spi_pio, prog, offset);
}

//! Helper function to setup common state machine configs (clock, pins) for all PIO programs.
//! @param[out] c State machine config.
static void setup_common_sm_config(pio_sm_config *c) {
	sm_config_set_clkdiv(c, spi_clock_div);
	sm_config_set_out_shift(c, false, true, 8u);
	sm_config_set_in_shift(c, false, true, 8u);
	sm_config_set_out_pins(c, spi_pin_tx, 1);
	sm_config_set_set_pins(c, spi_pin_csn, 1);
	sm_config_set_in_pins(c, spi_pin_rx);
	sm_config_set_sideset_pins(c, spi_pin_sck);
}

//! Helper function to start an operation within a session.
static inline void start_operation() {
	pio_interrupt_clear(spi_pio, 0u);
	op_pending = true;
	pio_sm_set_enabled(spi_pio, spi_sm, true);
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
		send_string("SPI operation didn't complete in time.\n");
		result = false;
	}
	else {
		result = true;
	}
	
	return result;
}

//! \b DMA_IRQ_0 handler for SPI DMA RX transfer.
//! @param[in,out] ints Interrupt state for \b DMA_IRQ_0. Corresponding bit is to be cleared if handled.
inline void own_spi_dma_irq0_handler(io_rw_32 *ints) {
	const uint32_t mask_rx = 1u << dma_channel_rx;
	
	if (*ints & mask_rx) {
		++dma_counter;
		*ints = mask_rx;
		dma_channel_set_write_addr(dma_channel_rx, dma_buf_rx + dma_counter, true);
	}
}

//! \b PIO0_IRQ_0 handler for state machine internal IRQ 0 (as SPI operation pending flag).
inline void own_spi_pio0_irq0_handler() {
	if (pio_interrupt_get(spi_pio, 0u)) {
		op_pending = false;
		pio_interrupt_clear(spi_pio, 0u);
	}
}

//! PIO subsystem initializer.
//! @param[in] pio Target PIO instance.
//! @param[in] pin_base Base GPIO index for SPI 4-pins, in {RX, CSN, SCK, TX} order.
//! @param[in] bit_rate Expected data bit rate, in bit per second.
//! @return True if no error has occurred.
bool own_spi_init(PIO pio, uint pin_base, uint32_t bit_rate) {
	bool result = true;
	
	spi_clock_div = (SYS_CLK_KHZ * 1000u) / (float)(bit_rate * 2u);
	spi_pin_csn = pin_base + 1u;
	spi_pin_rx = pin_base + 0u;
	spi_pin_sck = pin_base + 2u;
	spi_pin_tx = pin_base + 3u;
	spi_pio = pio;
	spi_sm = pio_claim_unused_sm(pio, false);
	
	if (spi_sm == -1) {
		send_string("No free state machine in target PIO for SPI.\n");
		result = false;
	}
	
	dma_buf_tx = calloc(DMA_BUF_SZ, sizeof(uint8_t));
	dma_channel_rx = dma_claim_unused_channel(false);
	dma_channel_tx = dma_claim_unused_channel(false);
	
	if (!dma_buf_tx) {
		send_string("Error allocating DMA target buffer(s) for SPI.\n");
		result = false;
	}
	if ((dma_channel_rx == -1) || (dma_channel_tx == -1)) {
		send_string("No free DMA channel(s) for SPI.\n");
		result = false;
	}
	
	if (result) {
		dma_channel_set_irq0_enabled(dma_channel_tx, false);
		
		//avoid glitching the bus when output mux switched from GPIO function to PIO. OE inversion is needed
		//to match output bit being sent, else it will get inverted if used as-is.
		//******************************************************************************
		pio_sm_config c = pio_get_default_sm_config();
		sm_config_set_set_pins(&c, spi_pin_rx, 4u);
		pio_sm_set_config(pio, spi_sm, &c);
		
		for (uint pin = spi_pin_rx; pin <= spi_pin_tx; ++pin) {
			gpio_pull_up(pin);
		}
		pio_sm_exec(pio, spi_sm, pio_encode_set(pio_pindirs, 0x0Fu));	//OE=1 -> output enabled
		pio_sm_exec(pio, spi_sm, pio_encode_set(pio_pins, 0x0Fu));	//set pins to 1
		for (uint pin = spi_pin_rx; pin <= spi_pin_tx; ++pin) {
			pio_gpio_init(pio, pin);
			gpio_set_oeover(pin, GPIO_OVERRIDE_INVERT);				//OE=1 -> output disabled = hi-Z
		}
		pio_sm_exec(pio, spi_sm, pio_encode_set(pio_pins, 0x00u));	//set pins to 0
		//******************************************************************************
		
		//map state machine internal IRQ to PIO IRQ as operation ended flag
		pio_interrupt_clear(pio, 0u);
		pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
	}
	
	return result;
}

//! Sends read command, blocking execution while operation is not finished.
//! @param[in] reg_addr Target device register address.
//! @param[out] data Output buffer to put received data from target device.
//! @param[in] data_sz Expected received data amount, in bytes. Must be 0 &lt; x &le; 256.
//! @return Received byte count, or -1 if error occurred during operation.
int own_spi_read_blocking(uint8_t reg_addr, uint8_t *data, size_t data_sz) {
	int result = -1;
	
	if (sess_pending) {
		send_string("Pending SPI session when trying to read.\n");
	}
	else if (!data_sz || (data_sz > 256u)) {
		send_string("Invalid SPI read size %zu.\n", data_sz);
	}
	else {
		sess_pending = true;
		
		if (pio_can_add_program(spi_pio, &own_spi_read_program)) {
			const uint offset = pio_add_program(spi_pio, &own_spi_read_program);
			pio_sm_config c = own_spi_read_program_get_default_config(offset);
			setup_common_sm_config(&c);
			pio_sm_init(spi_pio, spi_sm, offset, &c);
			
			spi_pio->txf[spi_sm] = (data_sz - 1u) << 24u;			//data byte count
			pio_sm_exec(spi_pio, spi_sm, pio_encode_out(pio_y, 8u));//scratch Y now hold the value
			spi_pio->txf[spi_sm] = (0x80u | reg_addr) << 24u;		//MSB replaced with read command (1)
			
			//DMA setup
			//******************************************************************************
			dma_buf_rx = data;
			dma_counter = 0u;
			
			{
				dma_channel_config dma_cfg_rx = dma_channel_get_default_config(dma_channel_rx);
				channel_config_set_dreq(&dma_cfg_rx, pio_get_dreq(spi_pio, spi_sm, false));
				channel_config_set_transfer_data_size(&dma_cfg_rx, DMA_SIZE_8);
				channel_config_set_read_increment(&dma_cfg_rx, false);
				channel_config_set_write_increment(&dma_cfg_rx, true);
				dma_channel_set_irq0_enabled(dma_channel_rx, true);
				dma_channel_configure(dma_channel_rx, &dma_cfg_rx, data, spi_pio->rxf + spi_sm, 1u, true);
			}
			//******************************************************************************
			
			start_operation();
			if (wait_operation_done()) {
				if (dma_counter == data_sz) {
					result = data_sz;
				}
				else {
					send_string("SPI read operation data size mismatch (expect:%u got:%u).\n", data_sz,
						dma_counter);
				}
			}
			cleanup_operation(&own_spi_read_program, offset);
			
			dma_channel_cleanup(dma_channel_rx);
			dma_buf_rx = NULL;
		}
		else {
			send_string("Error adding PIO program for SPI read.\n");
		}
		
		sess_pending = false;
	}
	
	return result;
}

//! Sends write command, blocking execution while operation is not finished.
//! @param[in] reg_addr Target device register address.
//! @param[in] data Output buffer to put received data from target device.
//! @param[in] data_sz Expected received data amount, in bytes. Must be 0 &lt; x &le; 256.
//! @return Sent byte count, or -1 if error occurred during operation.
int own_spi_write_blocking(uint8_t reg_addr, const uint8_t *data, size_t data_sz) {
	int result = -1;
	
	if (sess_pending) {
		send_string("Pending SPI session when trying to write.\n");
	}
	else if (!data_sz || (data_sz > 256u)) {
		send_string("Invalid SPI write size %zu.\n", data_sz);
	}
	else {
		sess_pending = true;
		
		if (pio_can_add_program(spi_pio, &own_spi_write_program)) {
			const uint offset = pio_add_program(spi_pio, &own_spi_write_program);
			pio_sm_config c = own_spi_write_program_get_default_config(offset);
			setup_common_sm_config(&c);
			pio_sm_init(spi_pio, spi_sm, offset, &c);
			
			spi_pio->txf[spi_sm] = data_sz << 24u;					//header + data byte count
			pio_sm_exec(spi_pio, spi_sm, pio_encode_out(pio_y, 8u));//scratch Y now hold the value
			spi_pio->txf[spi_sm] = (0x7Fu & reg_addr) << 24u;		//MSB replaced with write command (0)
			
			//DMA setup
			//******************************************************************************
			{
				dma_channel_config dma_cfg_tx = dma_channel_get_default_config(dma_channel_tx);
				channel_config_set_dreq(&dma_cfg_tx, pio_get_dreq(spi_pio, spi_sm, true));
				channel_config_set_transfer_data_size(&dma_cfg_tx, DMA_SIZE_8);
				channel_config_set_read_increment(&dma_cfg_tx, true);
				channel_config_set_write_increment(&dma_cfg_tx, false);
				dma_channel_configure(dma_channel_tx, &dma_cfg_tx, spi_pio->txf + spi_sm, data, data_sz,
					true);
			}
			//******************************************************************************
			
			start_operation();
			if (wait_operation_done()) {
				result = data_sz;
			}
			cleanup_operation(&own_spi_write_program, offset);
			
			dma_channel_cleanup(dma_channel_tx);
		}
		else {
			send_string("Error adding PIO program for SPI write.\n");
		}
		
		sess_pending = false;
	}
	
	return result;
}
