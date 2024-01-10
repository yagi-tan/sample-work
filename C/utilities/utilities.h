#ifndef UTILITIES_H
#define UTILITIES_H

#include <stdarg.h>
#include <stdint.h>

void send_data(char c);
void send_string(const char *fmt, ...);

void convert_to_hex(const uint8_t *data, uint32_t length, char *buf);

#endif
