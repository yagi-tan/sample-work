#include "logic_analyser.h"
#include "interrupts.h"
#include "own_spi.h"
#include "utilities.h"

#include <bmp2.h>

#include <pico/binary_info.h>
#include <pico/stdlib.h>

#include <stdio.h>
#include <stdlib.h>

#define SPI_PIN_RX			12
#define SPI_PIN_CSN			(SPI_PIN_RX + 1)
#define SPI_PIN_SCK			(SPI_PIN_RX + 2)
#define SPI_PIN_TX			(SPI_PIN_RX + 3)
#define SPI_PIO_INSTANCE	pio0
#define UART_PIN_TX 		0

#define LCAP_PIO_INSTANCE	pio1

capture_pin_group_config capture_pin_group_cfgs[1u] = {
	{
		.buf = NULL,
		.rate = 300000u,
		.sample_count = 384000u,
		.pin_base = SPI_PIN_RX,
		.pin_count = 4u
	}
};

//! BMP2 API callback for delay using host-specific function.
//! @param[in] period Delay period, in usecs.
//! @param[in,out] intf_ptr Interface pointer. Unused.
void bmp_delay(uint32_t period, void *intf_ptr) {
	(void) intf_ptr;
	sleep_us(period);
}

//! BMP2 API callback for sending SPI read command using self-implemented function.
//! @param[in] reg_addr Register address.
//! @param[out] reg_data Register data as read operation result.
//! @param[in] length \b req_data size, in bytes.
//! @param[in,out] intf_ptr Interface pointer. Unused.
//! @return \b BMP2_INTF_RET_SUCCESS if operation is successful, else \b BMP2_E_COM_FAIL.
BMP2_INTF_RET_TYPE bmp_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, const void *intf_ptr) {
	(void) intf_ptr;
	
	BMP2_INTF_RET_TYPE result;
	
	if (own_spi_read_blocking(reg_addr, reg_data, length) == (int) length) {
		result = BMP2_INTF_RET_SUCCESS;
		
		char hexa[length * 2u + 1u];
		convert_to_hex(reg_data, length, hexa);
		send_string("%s: @%2X data(%u):%s\n", __FUNCTION__, reg_addr, length, hexa);
	}
	else {
		send_string("SPI read error when trying to read 0x%X.\n", reg_addr);
		result = BMP2_E_COM_FAIL;
	}
	
	return result;
}

//! BMP2 API callback for sending SPI write command using self-implemented function.
//! @param[in] reg_addr Register address.
//! @param[in] reg_data Register data to be written. May contain register-data pair for burst write.
//! @param[in] length \b req_data size, in bytes. Expected to be x <= (\b BMP2_MAX_LEN * 2 - 1).
//! @param[in,out] intf_ptr Interface pointer. Unused.
//! @return \b BMP2_INTF_RET_SUCCESS if operation is successful, else \b BMP2_E_COM_FAIL for communication
//!			failure or \b BMP2_E_INVALID_LEN if data size is too large.
BMP2_INTF_RET_TYPE bmp_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length,
const void *intf_ptr) {
	(void) intf_ptr;
	
	BMP2_INTF_RET_TYPE result;
	
	{
		char hexa[length * 2u + 1u];
		convert_to_hex(reg_data, length, hexa);
		send_string("%s: @%2X data(%u):%s\n", __FUNCTION__, reg_addr, length, hexa);
	}
	
	if (own_spi_write_blocking(reg_addr, reg_data, length) == (int) length) {
		result = BMP2_INTF_RET_SUCCESS;
	}
	else {
		send_string("SPI write error when trying to write 0x%X.\n", reg_addr);
		result = BMP2_E_COM_FAIL;
	}
	
	return result;
}

int main() {
	struct bmp2_dev bmp_dev = {
		.delay_us = bmp_delay,
		.intf = BMP2_SPI_INTF,
		.intf_ptr = NULL,
		.power_mode = BMP2_POWERMODE_NORMAL,
		.read = bmp_read,
		.write = bmp_write
	};
	uint64_t sampling_time = 0u;
	bool result = true;
	
	//host setup
	//*********************************************************************************
	bi_decl(bi_program_description("BMP280 communication through SPI using PIO."));
	bi_decl(bi_1pin_with_func(UART_PIN_TX, GPIO_FUNC_UART));
	bi_decl(bi_4pins_with_func(SPI_PIN_RX, SPI_PIN_CSN, SPI_PIN_SCK, SPI_PIN_TX, GPIO_FUNC_PIO0));
	
	stdio_uart_init_full(uart0, 115200, UART_PIN_TX, -1);
	//*********************************************************************************
	
	send_string("BMP280 PIO SPI.\n");
	result &= setup_interrupts();
	
	//logic analyser setup
	//*********************************************************************************
	if (result) {
		start_logic_analyser(capture_pin_group_cfgs, 1u, LCAP_PIO_INSTANCE, SYS_CLK_KHZ * 1000u);
		
		// Wait for it to start up
		while (is_logic_analyser_active() && !is_logic_analyser_started()) {
			tight_loop_contents();
		}
	}
	//**********************************************************************************
	
	//PIO-based SPI setup
	result &= own_spi_init(SPI_PIO_INSTANCE, SPI_PIN_RX, 100000u);
	
	//BMP2 API setup
	//**********************************************************************************
	if (!result) {}
	else if (bmp2_init(&bmp_dev) == BMP2_OK) {
		send_string("BMP2 API init passed.\n");
		
		struct bmp2_config conf = {
			.filter = BMP2_FILTER_COEFF_16,
			.odr = BMP2_ODR_1000_MS,
			.os_mode = BMP2_OS_MODE_STANDARD_RESOLUTION,
			.spi3w_en = BMP2_SPI3_WIRE_DISABLE
		};
		
		if (bmp2_set_power_mode(BMP2_POWERMODE_NORMAL, &conf, &bmp_dev) == BMP2_OK) {
			send_string("BMP2 API config setup done.\n");
			
			uint32_t tmp;
			bmp2_compute_meas_time(&tmp, &conf, &bmp_dev);
			sampling_time = tmp;
		}
		else {
			send_string("BMP2 API config setup failed.\n");
			result = false;
		}
	}
	else {
		send_string("BMP2 API init failed.\n");
		result = false;
	}
	
	if (result) {
		uint8_t val_chg = 0x00, val_orig = 0x00, val_test = 0x64, reg_addr = BMP2_REG_CTRL_MEAS;
		
		bmp2_get_regs(BMP2_REG_CTRL_MEAS, &val_orig, 1u, &bmp_dev);
		bmp2_soft_reset(&bmp_dev);
		
		if (bmp2_set_regs(&reg_addr, &val_test, 1u, &bmp_dev) == BMP2_OK) {
			bmp2_get_regs(BMP2_REG_CTRL_MEAS, &val_chg, 1u, &bmp_dev);
			send_string("Register %02X value %02X -> %02X\n", BMP2_REG_CTRL_MEAS, val_orig, val_chg);
			
			reg_addr = BMP2_REG_CTRL_MEAS;
			bmp2_set_regs(&reg_addr, &val_orig, 1u, &bmp_dev);
		}
		else {
			send_string("Error writing test value to BMP register.\n");
			result = false;
		}
	}
	//**********************************************************************************
	
	if (result) {
		struct bmp2_data bmp_data;
		
		for (uint8_t idx = 0u; /*is_logic_analyser_active() && */(idx < 10u); ++idx) {
			const int8_t ret = bmp2_get_sensor_data(&bmp_data, &bmp_dev);
			
			if (ret == BMP2_OK) {
				send_string("[%u] p:%.4f t:%.4f\n", idx, bmp_data.pressure, bmp_data.temperature);
			}
			else {
				send_string("Error getting sensor data: %d\n", ret);
			}
			
			sleep_us(sampling_time);
		}
	}
	
	//sleep_ms(2000u);
	//print_logic_analyser_result();
	
	send_string("Program exiting...\n\n");
	
	return 0;
}
