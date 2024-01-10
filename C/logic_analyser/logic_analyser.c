#include "logic_analyser.h"
#include "utilities.h"

#include <hardware/dma.h>
#include <pico/multicore.h>

#include <assert.h>
#include <stdlib.h>

capture_pin_group_config *own_cfgs = NULL;
PIO own_pio = NULL;
size_t own_cfg_count = 0u;
float own_sys_clock = 0u;
bool logic_analyser_active = true, logic_analyser_started = false;

//! @todo Too lazy to consider byte-boundary for possible DMA transfer optimization (1 byte->2/4 byte).
//!		  Currently all data transfers (ISR->FIFO, DMA) will be in 1-byte size.
static bool setup_capture(capture_pin_group_config *capture_cfg, PIO pio) {
	assert(capture_cfg);
	assert((capture_cfg->pin_count == 1u) || (capture_cfg->pin_count == 2u) ||
		(capture_cfg->pin_count == 4u));
	
	const uint16_t capture_prog_instr = pio_encode_in(pio_pins, capture_cfg->pin_count);
	const struct pio_program capture_prog = {
		.instructions = &capture_prog_instr,
		.length = 1u,
		.origin = -1
	};
	const int channel = dma_claim_unused_channel(false), sm = pio_claim_unused_sm(pio, false);
	bool result = true;
	
	{
		const uint32_t bit_count = capture_cfg->sample_count * capture_cfg->pin_count;
		capture_cfg->buf_sz = (bit_count / 8u) + (bool)(bit_count % 8);
		capture_cfg->buf = calloc(capture_cfg->buf_sz, sizeof(uint8_t));
	}
	
	if (!capture_cfg->buf) {
		send_string("Error allocating DMA write target buffer.\n");
		result = false;
	}
	else if (channel == -1) {
		send_string("No free DMA channel.\n");
		result = false;
	}
	else if (sm == -1) {
		send_string("No free state machine in target PIO.\n");
		result = false;
	}
	else if (!pio_can_add_program(pio, &capture_prog)) {
		send_string("Error adding PIO program.\n");
		result = false;
	}
	else
	{
		const uint offset = pio_add_program(pio, &capture_prog);
		
		pio_sm_config sm_cfg = pio_get_default_sm_config();
		sm_config_set_clkdiv(&sm_cfg, own_sys_clock / capture_cfg->rate);
		sm_config_set_in_pins(&sm_cfg, capture_cfg->pin_base);
		sm_config_set_in_shift(&sm_cfg, false, true, 8u);
		sm_config_set_wrap(&sm_cfg, offset, offset);
		sm_config_set_fifo_join(&sm_cfg, PIO_FIFO_JOIN_RX);
		pio_sm_init(pio, sm, offset, &sm_cfg);
		
		dma_channel_config dma_cfg = dma_channel_get_default_config(channel);
		channel_config_set_dreq(&dma_cfg, pio_get_dreq(pio, sm, false));
		channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
		channel_config_set_read_increment(&dma_cfg, false);
		channel_config_set_write_increment(&dma_cfg, true);
		dma_channel_configure(channel, &dma_cfg, capture_cfg->buf, pio->rxf + sm, capture_cfg->buf_sz, true);
		
		capture_cfg->dma_channel = channel;
		capture_cfg->pio_sm = sm;
	}
	
	return result;
}

static void core1_entry() {
	bool result = true;
	
	for (size_t cfg_idx = 0u; cfg_idx < own_cfg_count; ++cfg_idx) {
		result &= setup_capture(own_cfgs + cfg_idx, own_pio);
	}
	
	if (result) {
		logic_analyser_started = true;
		
		for (size_t cfg_idx = 0u; cfg_idx < own_cfg_count; ++cfg_idx) {
			pio_sm_set_enabled(own_pio, own_cfgs[cfg_idx].pio_sm, true);
		}
		for (size_t cfg_idx = 0u; cfg_idx < own_cfg_count; ++cfg_idx) {
			dma_channel_wait_for_finish_blocking(own_cfgs[cfg_idx].dma_channel);
		}
	}
	
	logic_analyser_active = false;
}

//! Getter for logic analyser status.
//! @return True if logic analyser is still active (not ended/has terminated).
bool is_logic_analyser_active() {
	return logic_analyser_active;
}

//! Getter for logic analyser status.
//! @return True if logic analyser has started.
bool is_logic_analyser_started() {
	return logic_analyser_started;
}

//! Prints logic analyser result as binary data.
//! @return True if logic analyser has started before this and not active (has finished running).
bool print_logic_analyser_result() {
	bool result;
	
	if (!is_logic_analyser_started()){
		send_string("Logic analyser has not started for result printing.\n");
		result = false;
	}
	else if (is_logic_analyser_active()) {
		send_string("Logic analyser is still active for result printing.\n");
		result = false;
	}
	else {
		for (size_t cfg_idx = 0u; cfg_idx < own_cfg_count; ++cfg_idx) {
			capture_pin_group_config *const capture_cfg = own_cfgs + cfg_idx;
			size_t idx = 0u;
			uint8_t mask = 0u, mask_shift = 0u;
			
			for (uint8_t pin = 0u; pin < capture_cfg->pin_count; ++pin) {
				mask |= (1u << pin);
			}
			
			send_string("{base:%u count:%u sample:%u rate:%u start}", capture_cfg->pin_base,
				capture_cfg->pin_count, capture_cfg->sample_count, capture_cfg->rate);
			for (uint32_t sample_idx = 0u; sample_idx < capture_cfg->sample_count; ++sample_idx) {
				assert(idx < capture_cfg->buf_sz);
				
				send_data((capture_cfg->buf[idx] & (mask << mask_shift)) >> mask_shift);
				
				mask_shift += capture_cfg->pin_count;
				if (mask_shift >= 8u) {
					++idx;
					mask_shift = 0u;
				}
			}
			send_string("{base:%u count:%u sample:%u rate:%u end}", capture_cfg->pin_base,
				capture_cfg->pin_count, capture_cfg->sample_count, capture_cfg->rate);
		}
		
		result = true;
	}
	
	return result;
}

//! @param[in] sys_clock System clock, in Hz.
void start_logic_analyser(capture_pin_group_config *cfgs, size_t cfg_count, PIO pio, float sys_clock) {
	own_cfgs = cfgs;
	own_cfg_count = cfg_count;
	own_pio = pio;
	own_sys_clock = sys_clock;
	
	multicore_launch_core1(core1_entry);
}
