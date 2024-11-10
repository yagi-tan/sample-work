#ifndef GENERATOR_H
#define GENERATOR_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>

//not handling big-endian platform for now
static_assert(std::endian::native == std::endian::little, "Data format is expected to be little-endian.");

//! USB IN vendor request for notifying device to send channel readings.
#define USB_REQ_SEND_READING	50

//! Used in \ref ch_data structure.
#define SAMPLE_BITS				4u
#define SAMPLE_PER_READING		4u
#define TAG_BITS				28u

//! Format of data sent to (or received from) USB control endpoint to set (or get) channel config.
struct ch_config {
	//! Channel index or endpoint address, depending on direction.
	uint8_t idx;
	uint8_t pinbase;												//!< Pin base index.
	uint8_t pincount;												//!< Pin count.
	uint32_t rate;													//!< Sampling rate, in Hz.
} __attribute__ ((packed));

//! Format of data sent to client as single logic analyser reading.
struct ch_data {
	//! \ref data valid entry position. MSB-&gt;LSB bits = low-&gt;high index. Set bit = valid sample.
	uint32_t valid:SAMPLE_BITS;
	//! Ever increasing tag for this reading. May overflow to 0.
	uint32_t tag:TAG_BITS;
	//! Reading samples. Each sample LSB-&gt;MSB = low-&gt;high pin index.
	uint8_t data[SAMPLE_PER_READING];
} __attribute__ ((packed));
static_assert(SAMPLE_BITS == SAMPLE_PER_READING, "Sample bits must match sample count per reading.");
static_assert((SAMPLE_BITS + TAG_BITS) == (sizeof(uint32_t) * 8u), "Total field bits must match type size.");

//! Format of sample after interpreted.
struct ch_sample {
	uint64_t level:8u;												//!< Sample level/value.
	//! Monotonic sample timestamp by tracking reading tag and valid sample bits.
	uint64_t ts:56u;
};

bool generateData(uint8_t idx, std::deque<uint8_t> &data, size_t maxSz);
bool getGeneratorConfig(uint8_t idx, ch_config *cfg);
bool setGeneratorConfig(const ch_config *cfg);

bool interpretData(uint8_t idx, const ch_data *reading, std::deque<uint8_t> &data, size_t maxSz);
bool resetInterpreter(uint8_t idx);

#endif
