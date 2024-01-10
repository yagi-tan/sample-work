#include "utilities.h"

#include <stdio.h>

//! Sends raw 7-bit data to stdout, adjusted for serial (instead of TTL) UART transmission.
void send_data(char c) {
	putchar((c << 1) | 0x01);
}

//! Prints 7-bit string, replacing \b printf(), adjusted for serial (instead of TTL) UART transmission.
void send_string(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	
	static char buf[1024];
	const size_t len = vsnprintf(buf, sizeof(buf), fmt, args);
	
	if (len > 0) {
		for( size_t i = 0u; i < len; ++i) {
			putchar((buf[i] << 1) | 0x01);
		}
	}
}

//! Converts binary data into string in hex format.
//! @param[in] data Data to be converted.
//! @param[in] length Data length, in bytes.
//! @param[out] buf Output buffer to store the string. Must be x &ge; \b length * 2 + 1 (NULL terminated).
void convert_to_hex(const uint8_t *data, uint32_t length, char *buf) {
	for (uint32_t i = 0u; i < length; ++i) {
		const uint8_t val = data[i];
		
		for (uint8_t j = 0u; j < 2u; ++j) {
			const uint8_t part = (val & (0xF0u >> (j * 4u))) >> (4u * (j ^ 1u));
			buf[i * 2u + j] = (part > 9u) ? 'A' + (int8_t)(part - 10u) : '0' + (int8_t) part;
		}
	}
	buf[length * 2u] = '\0';
}
