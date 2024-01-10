#include "logic_analyser.h"
#include "interrupts.h"
#include "own_i2c.h"
#include "utilities.h"

#include <bmp2.h>

#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <pico/binary_info.h>
#include <pico/stdlib.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define I2C_BMP280_ADDR		BMP2_I2C_ADDR_PRIM
#define I2C_PIN_SDA			6
#define I2C_PIN_SCL			(I2C_PIN_SDA + 1)
#define I2C_PIO_INSTANCE	pio0
#define UART_PIN_TX			0

#define LCAP_PIO_INSTANCE	pio1

capture_pin_group_config capture_pin_group_cfgs[1u] = {
	{
		.buf = NULL,
		.rate = 1600000u,
		.sample_count = 160000u,
		.pin_base = I2C_PIN_SDA,
		.pin_count = 2u
	}
};

//! BMP2 API callback for delay using host-specific function.
//! @param[in] period Delay period, in usecs.
//! @param[in,out] intf_ptr Interface pointer. Unused.
void bmp_delay(uint32_t period, void *intf_ptr) {
	(void) intf_ptr;
	sleep_us(period);
}

//! BMP2 API callback for sending I2C read command using self-implemented function.
//! @param[in] reg_addr Register address.
//! @param[out] reg_data Register data as read operation result.
//! @param[in] length \b req_data size, in bytes.
//! @param[in,out] intf_ptr Interface pointer. Unused.
//! @return \b BMP2_INTF_RET_SUCCESS if operation is successful, else \b BMP2_E_COM_FAIL.
BMP2_INTF_RET_TYPE bmp_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, const void *intf_ptr) {
	(void) intf_ptr;
	
	BMP2_INTF_RET_TYPE result;
	
	if (own_i2c_write_blocking(I2C_BMP280_ADDR, &reg_addr, 1u, true) != 1) {
		send_string("I2C write error when trying to read 0x%X.\n", reg_addr);
		result = BMP2_E_COM_FAIL;
	}
	else if (own_i2c_read_blocking(I2C_BMP280_ADDR, reg_data, length, false) != (int) length) {
		send_string("I2C read error when trying to read 0x%X.\n", reg_addr);
		result = BMP2_E_COM_FAIL;
	}
	else {
		result = BMP2_INTF_RET_SUCCESS;
	}
	
	return result;
}

//! BMP2 API callback for sending I2C write command using self-implemented function.
//! @param[in] reg_addr Register address.
//! @param[in] reg_data Register data to be written. May contain register-data pair for burst write.
//! @param[in] length \b req_data size, in bytes. Expected to be x <= (\b BMP2_MAX_LEN * 2 - 1).
//! @param[in,out] intf_ptr Interface pointer. Unused.
//! @return \b BMP2_INTF_RET_SUCCESS if operation is successful, else \b BMP2_E_COM_FAIL for communication
//!			failure or \b BMP2_E_INVALID_LEN if data size is too large.
BMP2_INTF_RET_TYPE bmp_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length,
const void *intf_ptr) {
	(void) intf_ptr;
	
	uint8_t buf[BMP2_MAX_LEN * 2u];									//need first register address at front
	BMP2_INTF_RET_TYPE result;
	
	if (sizeof(buf) <= length) {
		send_string("Data length '%lu' too large when trying to write 0x%X.\n", length, reg_addr);
		result = BMP2_E_INVALID_LEN;
	}
	else {
		const size_t write_len = length + 1u;
		
		buf[0u] = reg_addr;
		memcpy(buf + 1u, reg_data, length);
		
		if (own_i2c_write_blocking(I2C_BMP280_ADDR, buf, write_len, false) != (int) write_len) {
			send_string("I2C write error when trying to write 0x%X.\n", reg_addr);
			result = BMP2_E_COM_FAIL;
		}
		else {
			result = BMP2_INTF_RET_SUCCESS;
		}
	}
	
	return result;
}

int main() {
	struct bmp2_dev bmp_dev = {
		.delay_us = bmp_delay,
		.intf = BMP2_I2C_INTF,
		.intf_ptr = NULL,
		.power_mode = BMP2_POWERMODE_NORMAL,
		.read = bmp_read,
		.write = bmp_write
	};
	uint64_t sampling_time = 0u;
	bool result = true;
	
	//host setup
	//*********************************************************************************
	bi_decl(bi_program_description("BMP280 communication through I2C using PIO."));
	bi_decl(bi_1pin_with_func(UART_PIN_TX, GPIO_FUNC_UART));
	bi_decl(bi_2pins_with_func(I2C_PIN_SCL, I2C_PIN_SDA, GPIO_FUNC_PIO0));
	
	stdio_uart_init_full(uart0, 115200, UART_PIN_TX, -1);
	//*********************************************************************************
	
	result &= setup_interrupts();
	
	//logic analyser setup
	//*********************************************************************************
	/*if (result) {
		start_logic_analyser(capture_pin_group_cfgs, 1u, LCAP_PIO_INSTANCE, SYS_CLK_KHZ * 1000u);
		
		// Wait for it to start up
		while (is_logic_analyser_active() && !is_logic_analyser_started()) {
			tight_loop_contents();
		}
	}*/
	//**********************************************************************************
	
	//PIO-based I2C setup
	result &= own_i2c_init(I2C_PIO_INSTANCE, I2C_PIN_SDA, 1000000u);
	
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
	//**********************************************************************************
	
	if (result) {
		struct bmp2_data bmp_data;
		
		for (uint8_t idx = 0u; /*is_logic_analyser_active() && (idx < 10u)*/; ++idx) {
			const int8_t ret = bmp2_get_sensor_data(&bmp_data, &bmp_dev);
			
			if (ret == BMP2_OK) {
				send_string("[%u] p:%.4f t:%.4f\n", idx, bmp_data.pressure, bmp_data.temperature);
				sleep_us(sampling_time);
			}
			else {
				send_string("Error getting sensor data: %d\n", ret);
				result = false;
			}
		}
	}
	
	/*sleep_ms(2000u);
	print_logic_analyser_result();*/
	
	send_string("Program exiting...\n");
	
	return 0;
}
