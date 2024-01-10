#ifndef	SITESIM_H
#define	SITESIM_H

#include <nlohmann/json.hpp>

#include <memory>
#include <string_view>

namespace hvac {
	//! Simulates site operating condition (noise, disconnection, fault).
	class SiteSim {
	public:
		static bool Init(const nlohmann::json &config);
	private:
		SiteSim(const nlohmann::json &config);
		
		static std::unique_ptr<SiteSim> sim;
	};
}

#endif
