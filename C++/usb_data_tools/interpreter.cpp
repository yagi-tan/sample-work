#include "data_tools.h"
#include "main.h"

#include <map>
#include <memory>
#include <new>

//! Single channel logic analyser readings data interpreter.
class Interpreter {
public:
	//! Constructor.
	Interpreter() noexcept {
		reset();
	}
	
	//! Processes readings data into separate valid sample(s) with timestamp.
	//! @param[in] reading Channel reading object.
	//! @param[in,out] data Storage to be filled with data.
	void proc(const ch_data *reading, std::deque<uint8_t> &data) {
		const uint8_t valid = reading->valid;
		
		ch_sample obj;
		const uint8_t *pobj = reinterpret_cast<uint8_t*>(&obj);
		
		if (hasSeen) [[likely]] {
			if (lastTag < reading->tag) [[likely]] {				//normal progression
				ts += ((reading->tag - lastTag) * SAMPLE_PER_READING);
			}
			else if (lastTag > reading->tag) {						//tag has overflowed
				constexpr uint64_t maxTag = 1u << TAG_BITS;
				ts += ((maxTag - lastTag + reading->tag) * SAMPLE_PER_READING);
			}
		}
		lastTag = reading->tag;
		
		for (uint8_t idx = 0u; idx < SAMPLE_PER_READING; ++idx) {
			if (valid & (0b1000u >> idx)) {
				obj.level = reading->data[idx];
				obj.ts = ts + idx;
				data.insert(data.end(), pobj, pobj + sizeof(obj));
				
				hasSeen = true;
			}
		}
	}
	
	//! Resets timestamp and tracker to 0.
	void reset() {
		hasSeen = false;
		lastTag = 0u;
		ts = 0ull;
	}
	
private:
	//! Monotonically increasing base timestamp for current tag (1 tag = \ref SAMPLE_PER_READING samples).
	uint64_t ts;
	uint32_t lastTag;												//!< Tracks last seen tag.
	bool hasSeen;													//!< Has seen valid sample after reset.
};

//! Channel index -&gt; interpreter object.
static std::map<uint8_t, std::shared_ptr<Interpreter>> channels;

//! Interprets readings data into separate samples with associated timestamp.
//! @param[in] idx Target channel index.
//! @param[in] reading Channel reading object.
//! @param[in,out] data Storage to be filled with data.
//! @param[in] maxSz \b data size won't be larger than this value after filling.
//! @return True if channel with specified index exists and there's enough space for data.
bool interpretData(uint8_t idx, const ch_data *reading, std::deque<uint8_t> &data, size_t maxSz) {
	if (data.size() >= maxSz) {
		SPDLOG_WARN("Storage already full for channel {} data interpreter.", idx);
		return true;
	}
	
	if ((maxSz - data.size()) < (sizeof(ch_sample) * SAMPLE_PER_READING)) {
		SPDLOG_ERROR("Storage space not enough for channel {} data interpreter.", idx);
		return false;
	}
	
	auto iter = channels.find(idx);
	if (iter == channels.end()) {
		SPDLOG_ERROR("Channel {} not found for data interpreter.", idx);
		return false;
	}
	
	std::shared_ptr<Interpreter> channel{iter->second};
	channel->proc(reading, data);
	
	return true;
}

//! Resets existing channel tag tracking (timestamp to 0). Will add the channel if not exists.
//! @param[in] idx Target channel index.
//! @return False if channel object can't be allocated when needed, true otherwise.
bool resetInterpreter(uint8_t idx) {
	auto iter = channels.find(idx);
	
	if (iter == channels.end()) {
		auto obj = new(std::nothrow) Interpreter();
		
		if (obj) {
			std::shared_ptr<Interpreter> channel{obj};
			channels.emplace(idx, channel);
			
			SPDLOG_INFO("Channel {} added.", idx);
		}
		else {
			SPDLOG_ERROR("Error allocating new channel {} object.", idx);
			return false;
		}
	}
	else {
		iter->second->reset();
		SPDLOG_INFO("Channel {} reset.", idx);
	}
	
	return true;
}
