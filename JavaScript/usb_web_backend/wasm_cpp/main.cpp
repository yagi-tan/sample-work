#include "data_tools.h"

#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <emscripten/emscripten.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <vector>

//! Helper function to validate 'cfg' data size.
//! @param[in] cfgSz Channel configuration data size, in bytes.
//! @return True if \b cfgSz matches \ref ch_config size.
bool validateCfgSize(size_t cfgSz) {
	if (cfgSz != sizeof(ch_config)) {
		SPDLOG_ERROR("Data size '{}' byte(s) mismatch.", cfgSz);
		return false;
	}
	
	return true;
}

extern "C" {
	//! Initialises overall system. May call abort() due to unhandled exception in spdlog setup code.
	//! @return Always true.
	EMSCRIPTEN_KEEPALIVE bool initSys() {
		std::vector<spdlog::sink_ptr> sinks;
		spdlog::sink_ptr sink;
		
		sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
		sink->set_level(spdlog::level::info);
		sink->set_pattern("%Y%m%dT%H%M%S,%e %L [%s:%#] %v");		//check 'tweakme.h' for config
		sinks.push_back(sink);
		
		std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>("log", sinks.begin(),
			sinks.end());
		logger->flush_on(spdlog::level::trace);						//set level at 'main.h' instead
		logger->set_level(spdlog::level::trace);
		spdlog::set_default_logger(logger);							//we'll be using macro
		
		return true;
	}
	
	//! Shuts down overall system.
	EMSCRIPTEN_KEEPALIVE void exitSys() {
		SPDLOG_INFO("{}: Program ended. Exiting...", __FUNCTION__);
		spdlog::shutdown();
	}
	
	//! Glue function for \ref getGeneratorConfig().
	//! @param[in] idx Target channel index.
	//! @param[out] cfg Channel configuration data.
	//! @param[in] cfgSz Channel configuration data size, in bytes.
	//! @return False if \b cfgSz doesn't match \ref ch_config size. Else, as per target function.
	EMSCRIPTEN_KEEPALIVE bool getConfig(uint8_t idx, ch_config *cfg, size_t cfgSz) {
		return validateCfgSize(cfgSz) ? getGeneratorConfig(idx, cfg) : false;
	}
	
	//! Glue function for \ref generateData().
	//! @param[in] idx Target channel index.
	//! @param[out] data Channel readings data.
	//! @param[in] dataSz Channel readings data size, in bytes.
	//! @return Readings data count, in bytes. -1 if error has occurred.
	EMSCRIPTEN_KEEPALIVE int32_t getData(uint8_t idx, uint8_t *data, size_t dataSz) {
		std::deque<uint8_t> dataTmp;
		
		if (generateData(idx, dataTmp, dataSz)) [[likely]] {
			std::copy(dataTmp.cbegin(), dataTmp.cend(), data);
			return dataTmp.size();
		}
		
		return -1;
	}
	
	//! Glue function for \ref interpretData().
	//! @param[in] idx Target channel index.
	//! @param[in] reading Channel reading object.
	//! @param[out] data Channel sample data.
	//! @param[in] dataSz Channel sample data size, in bytes.
	//! @return Reading sample count, in bytes. -1 if error has occurred.
	EMSCRIPTEN_KEEPALIVE int32_t procData(uint8_t idx, const ch_data *reading, uint8_t *data, size_t dataSz) {
		std::deque<uint8_t> dataTmp;
		
		if (interpretData(idx, reading, dataTmp, dataSz)) [[likely]] {
			std::copy(dataTmp.cbegin(), dataTmp.cend(), data);
			return dataTmp.size();
		}
		
		return -1;
	}
	
	//! Glue function for \ref resetInterpreter().
	//! @param[in] idx Target channel index.
	//! @return \ref resetInterpreter() return value.
	EMSCRIPTEN_KEEPALIVE bool resetProc(uint8_t idx) {
		return resetInterpreter(idx);
	}
	
	//! Glue function for \ref setGeneratorConfig().
	//! @param[out] cfg Channel configuration data.
	//! @param[in] cfgSz Channel configuration data size, in bytes.
	//! @return False if \b cfgSz doesn't match \ref ch_config size. Else, as per target function.
	EMSCRIPTEN_KEEPALIVE bool setConfig(const ch_config *cfg, size_t cfgSz) {
		return validateCfgSize(cfgSz) ? setGeneratorConfig(cfg) : false;
	}
}
