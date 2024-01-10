#include "logic_analyser.h"
#include "utilities.h"

#include <hardware/gpio.h>
#include <pico/binary_info.h>
#include <pico/stdlib.h>

#define	LED_PIN			25
#define PIO_INSTANCE	pio0
#define UART_PIN_TX		0

capture_pin_group_config capture_pin_group_cfgs[] = {
	{
		.buf = NULL,
		.rate = 400000u,
		.sample_count = 1048576u,
		.pin_base = 25u,
		.pin_count = 1u
	},
	{
		.buf = NULL,
		.rate = 500u,
		.sample_count = 2500u,
		.pin_base = 6u,
		.pin_count = 2u
	}
};

int main() {
	//host setup
	//*********************************************************************************
	bi_decl(bi_program_description("Logic analyser by sampling pins."));
	bi_decl(bi_1pin_with_name(LED_PIN, "On-board LED"));
	bi_decl(bi_1pin_with_func(UART_PIN_TX, GPIO_FUNC_UART));
	
	stdio_uart_init_full(uart0, 115200, UART_PIN_TX, -1);
	
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	//*********************************************************************************	
	
	start_logic_analyser(capture_pin_group_cfgs,
		sizeof(capture_pin_group_cfgs) / sizeof(*capture_pin_group_cfgs), PIO_INSTANCE, SYS_CLK_MHZ);
	
	// Wait for it to start up
	while (is_logic_analyser_active() && !is_logic_analyser_started()) {
		tight_loop_contents();
	}
	
	while (is_logic_analyser_active()) {
		gpio_put(LED_PIN, true);
		sleep_ms(250);
		gpio_put(LED_PIN, false);
		sleep_ms(250);
	}
}
