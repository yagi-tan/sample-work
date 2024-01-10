#include "main.h"
#include "DevManager.h"
#include "SiteSim.h"

#include <Commons.h>

#include <nlohmann/json.hpp>

//! Initializes site simulator (as singleton object).
//! @param config Configuration object.
//! @return True if manager already initialized or initialized successfully.
bool hvac::SiteSim::Init(const nlohmann::json &config) {
	bool result = true;

	if (!sim) {
		try {
			sim.reset(new SiteSim(config));
		}
		catch (const std::exception &e) {
			SPDLOG_ERROR("Error initializing simulator: '{}'", e.what());
			result = false;
		}
	}

	return result;
}

//! Constructor.
//! @param config Configuration object.
//! @throw invalid_argument If configuration JSON type is not object.
hvac::SiteSim::SiteSim(const nlohmann::json &config) {
	if (!config.is_object()) {
		throw std::invalid_argument("Invalid configuration JSON type.");
	}
	
	return;
}

std::unique_ptr<hvac::SiteSim> hvac::SiteSim::sim;
