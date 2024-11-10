#include "data_tools.h"
#include "main.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <set>

//! Single channel data generator as logic analyser readings.
class Generator {
	using steady_clock = std::chrono::steady_clock;
	using time_point = std::chrono::time_point<steady_clock>;
	
public:
	//! Constructor.
	Generator() noexcept {
		cfg.idx = 0u;
		cfg.pinbase = 0u;
		cfg.pincount = 0u;
		cfg.rate = 0u;
		
		resetTracker();
	}
	
	//! Destructor.
	virtual ~Generator() {
		SPDLOG_WARN("Channel {} removed.", cfg.idx);
	}
	
	//! Get channel readings.
	//! @param[out] data Storage to be filled with readings.
	//! @param[in] count How many \ref ch_data objects to be inserted. Must be &gt; 0.
	//! @return True if there's at least a new object being added to \b data.
	bool getData(std::deque<uint8_t> &data, uint32_t count) {
		assert(count);
		
		const time_point now = steady_clock::now();
		const uint64_t smps = std::chrono::duration<double>(now - lastReadTs).count() * cfg.rate;
		
		if (!smps) [[unlikely]] {
			SPDLOG_WARN("Channel {} has no new sample since last reading.", cfg.idx);
			return false;
		}
		
		//calculate available samples/readings for latest (right-side) readings
		//******************************************************************************
		uint8_t smpsFill = SAMPLE_PER_READING - lastReadQt;
		uint64_t smpsRight = smps;
		
		//last reading is incomplete so need to fill with current sample
		if (lastReadQt) {
			if (smpsRight > smpsFill) {
				smpsRight -= smpsFill;
			}
			else {
				smpsRight = 0u;
			}
		}
		
		//rightmost reading may have incomplete group of samples
		const uint64_t readsRight = smpsRight / SAMPLE_PER_READING + !!(smpsRight % SAMPLE_PER_READING);
		//******************************************************************************
		
		ch_data obj;
		const uint8_t *pobj = reinterpret_cast<uint8_t*>(&obj);
		
		//! Helper function to add latest (right-side) readings.
		auto addReading = [this, &data, &obj, &smpsRight, pobj]() -> void {
			obj.valid = (1u << SAMPLE_BITS) - 1u;
			lastReadQt = smpsRight % SAMPLE_PER_READING;			//last reading sample count
			
			while (smpsRight) {
				++obj.tag;
				
				if (smpsRight >= SAMPLE_PER_READING) [[likely]] {
					addSample(obj.data, 0u, SAMPLE_PER_READING);
					smpsRight -= SAMPLE_PER_READING;
				}
				else {
					addSample(obj.data, 0u, smpsRight);
					obj.valid <<= (SAMPLE_PER_READING - smpsRight);
					smpsRight = 0u;
				}
				
				data.insert(data.end(), pobj, pobj + sizeof(obj));
			}
		};
		
		if (readsRight >= count) {									//latest readings are enough
			obj.tag = tag + (readsRight - count);
			smpsRight -= ((readsRight - count) * SAMPLE_PER_READING);	//remove unused samples
			addReading();
		}
		else if (readsRight) {										//complete last reading + new readings
			obj.tag = tag;
			
			if (lastReadQt) {
				addSample(obj.data, lastReadQt, smpsFill);			//clear already sent data
				obj.valid = (1u << smpsFill) - 1u;
				data.insert(data.end(), pobj, pobj + sizeof(obj));
			}
			
			addReading();
		}
		else {														//only enough to fill last reading
			smpsFill = std::min((uint64_t) smpsFill, smps);			//check if sample is enough for filling
			
			addSample(obj.data, lastReadQt, smpsFill);				//clear already sent data
			obj.tag = tag;
			obj.valid = ((1u << smpsFill) - 1u) << (SAMPLE_PER_READING - lastReadQt - smpsFill);
			
			lastReadQt += smpsFill;
			if (lastReadQt >= SAMPLE_PER_READING) {
				lastReadQt = 0u;
			}
			data.insert(data.end(), pobj, pobj + sizeof(obj));
		}
		
		lastReadTs = now;
		tag = obj.tag;
		
		return true;
	}
	
	//! Getter for channel config.
	//! @return Reference to config object.
	const ch_config& getConfig() {
		return cfg;
	}
	
	//! Sets channel config. Resets tracker if config changes.
	//! @param[in] cfg New channel config.
	//! @return True if \b cfg is valid.
	bool setConfig(const ch_config *cfg) {
		if (validateConfig(cfg)) {
			if (memcmp(cfg, &this->cfg, sizeof(ch_config))) {
				SPDLOG_INFO("Channel {} config set - base:{} count:{} rate:{}", cfg->idx, cfg->pinbase,
							cfg->pincount, cfg->rate);
				
				this->cfg = *cfg;
				resetTracker();
			}
			else {
				SPDLOG_INFO("Channel {} config kept unchanged.", cfg->idx);
			}
			
			return true;
		}
		
		return false;
	}
	
	//! Helper function to validate channel config.
	//! @param[in] cfg New channel config.
	//! @return True if \b cfg is valid.
	static bool validateConfig(const ch_config *cfg) noexcept {
		if (cfg->pinbase >= 26u) {									//there're 26 GPIO pins on Pico
			SPDLOG_ERROR("Invalid pin base '{}' as channel config.", cfg->pincount);
			return false;
		}
		
		std::set<uint8_t> validPinCounts{1u, 2u, 4u, 8u};				//only these values are acceptable
		if (!validPinCounts.contains(cfg->pincount)) {
			SPDLOG_ERROR("Invalid pin count '{}' as channel config.", cfg->pincount);
			return false;
		}
		
		if ((!cfg->rate) || (cfg->rate > 125'000'000u)) {			//125MHz as per default Pico system clock
			SPDLOG_ERROR("Invalid rate '{}' as channel config.", cfg->rate);
			return false;
		}
		
		return true;
	}
	
private:
	//! Add sample(s) to reading data. Region outside of request will be zeroed out.
	//! @param[out] data Reading data/sample array.
	//! @param[in] start Array start index, inclusive. 0 &le; x &lt; \ref SAMPLE_PER_READING.
	//! @param[in] count Sample count to be added. Must be \b start + \b count &le; \ref SAMPLE_PER_READING.
	void addSample(uint8_t *data, uint8_t start, uint8_t count) {
		assert((start >= 0) && (start < SAMPLE_PER_READING));
		assert((start + count) <= SAMPLE_PER_READING);
		
		const uint8_t end = start + count;
		std::fill_n(data, start, 0u);								//outer left region
		std::fill_n(data + end, SAMPLE_PER_READING - end, 0u);		//outer right region
		
		for (uint8_t idx = start; idx < end; ++idx) {
			//rightmost pin (lowest index) has fastest level change (every sample). next pin doubles the
			//clock, and so on.
			data[idx] = (level++) & ((1u << cfg.pincount) - 1u);
		}
	}
	
	//! Helper function to reset tracking variables' value.
	void resetTracker() {
		lastReadTs = steady_clock::now();
		lastReadQt = 0u;
		tag = (1u << TAG_BITS) - 1u;								//+1 will overflow (0) the value
		level = 0u;
	}
	
	ch_config cfg;
	time_point lastReadTs;
	uint32_t tag;													//!< Always points to last used value.
	uint8_t lastReadQt;
	uint8_t level;													//!< Level tracker for all pins.
};

static std::map<uint8_t, std::shared_ptr<Generator>> channels;		//!< Channel index -&gt; generator object.

//! Generates data for a channel. For now it's just random data.
//! @param[in] idx Channel index.
//! @param[in,out] data Storage to be filled with data.
//! @param[in] maxSz \b data size won't be larger than this value after filling.
//! @return True if channel with specified index exists and there's enough space for data.
bool generateData(uint8_t idx, std::deque<uint8_t> &data, size_t maxSz) {
	if (data.size() >= maxSz) {
		SPDLOG_WARN("Storage already full for channel {} data generation.", idx);
		return true;
	}
	
	const uint32_t readingCount = (maxSz - data.size()) / sizeof(ch_data);
	if (!readingCount) {
		SPDLOG_ERROR("Storage space not enough for channel {} data generation.", idx);
		return false;
	}
	
	auto iter = channels.find(idx);
	if (iter == channels.end()) {
		SPDLOG_ERROR("Channel {} not found for data generation.", idx);
		return false;
	}
	
	std::shared_ptr<Generator> channel{iter->second};
	channel->getData(data, readingCount);
	
	return true;
}

//! Gets logic analyser channel data generator config.
//! @param[in] idx Target channel index.
//! @param[out] cfg Channel configuration data.
//! @return True if target channel is found.
bool getGeneratorConfig(uint8_t idx, ch_config *cfg) {
	auto iter = channels.find(idx);
	
	if (iter == channels.end()) {
		SPDLOG_ERROR("Channel {} not found to get config.", idx);
		return false;
	}
	
	*cfg = std::shared_ptr<Generator>{iter->second}->getConfig();
	
	return true;
}

//! Sets logic analyser channel data generator config. Will add the channel if not exists. Will remove
//! existing channel if \b cfg is not valid.
//! @param[in] cfg New channel config.
//! @return True as long \b cfg is valid, regardless whether config is unchanged.
bool setGeneratorConfig(const ch_config *cfg) {
	auto iter = channels.find(cfg->idx);
	
	if (iter == channels.end()) {
		if (Generator::validateConfig(cfg)) {						//ensure valid config before obj creation
			auto obj = new(std::nothrow) Generator();
			
			if (obj) {
				obj->setConfig(cfg);
				
				std::shared_ptr<Generator> channel{obj};
				channels.emplace(cfg->idx, channel);
				
				SPDLOG_INFO("Channel {} added.", cfg->idx);
			}
			else {
				SPDLOG_ERROR("Error allocating new channel {} object.", cfg->idx);
				return false;
			}
		}
		else {
			return false;
		}
	}
	else {
		std::shared_ptr<Generator> channel{iter->second};
		
		if (!channel->setConfig(cfg)) {
			channels.erase(iter);
			return false;
		}
	}
	
	return true;
}
