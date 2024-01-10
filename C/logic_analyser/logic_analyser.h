#ifndef LOGIC_ANALYSER_H
#define LOGIC_ANALYSER_H

#include <hardware/pio.h>
#include <pico/types.h>

typedef struct {
	uint8_t *buf;													//!< DMA write destination.
	size_t buf_sz;													//!< Buffer size, in bytes.
	uint32_t rate;													//!< Sample per second.
	uint32_t sample_count;											//!< 1 sample = 1 bit per pin.
	uint dma_channel;
	uint pio_sm;
	uint8_t pin_base;
	uint8_t pin_count;												//!< Value must be {1,2,4}.
} capture_pin_group_config;

bool is_logic_analyser_active();
bool is_logic_analyser_started();
bool print_logic_analyser_result();
void start_logic_analyser(capture_pin_group_config *cfgs, size_t cfg_count, PIO pio, float sys_clock);

#endif
