#ifndef GENERATOR_H
#define GENERATOR_H

#include <linux/types.h>

#include <cstddef>
#include <cstdint>
#include <deque>

//! USB IN vendor request for notifying device to send channel readings.
#define USB_REQ_SEND_READING	50

//! Used in \ref ch_data structure.
#define SAMPLE_BITS				4u
#define SAMPLE_PER_READING		4u
#define TAG_BITS				28u

//! Format of data sent to (or received from) USB control endpoint to set (or get) channel config.
struct ch_config {
	//! Channel index or endpoint address, depending on direction.
	__u8 idx;
	__u8 pinbase;													//!< Pin base index.
	__u8 pincount;													//!< Pin count.
	__le32 rate;													//!< Sampling rate, in Hz.
} __attribute__ ((packed));

//! Format of data sent to client as single logic analyser reading.
struct ch_data {
	//! \ref data valid entry position. MSB-&gt;LSB bits = low-&gt;high index. Set bit = valid sample.
	__le32 valid:SAMPLE_BITS;
	//! Ever increasing tag for this reading. May overflow to 0.
	__le32 tag:TAG_BITS;
	//! Reading samples. Each sample LSB-&gt;MSB = low-&gt;high pin index.
	__u8 data[SAMPLE_PER_READING];
} __attribute__ ((packed));
static_assert(SAMPLE_BITS == SAMPLE_PER_READING, "Sample bits must match sample count per reading.");
static_assert((SAMPLE_BITS + TAG_BITS) == (sizeof(__le32) * 8u), "Total field bits must match type size.");

bool generateData(__u8 idx, std::deque<uint8_t> &data, size_t maxSz);
bool getGeneratorConfig(__u8 idx, ch_config *cfg);
bool setGeneratorConfig(const ch_config *cfg);

#endif
