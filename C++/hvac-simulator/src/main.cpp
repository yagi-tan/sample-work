#include "main.h"
#include "BacServer.h"
#include "DevManager.h"
#include "SiteSim.h"
#include "WebServer.h"

#define SPDLOG_COMPILED_LIB
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>

#include <Commons.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <experimental/filesystem>
#include <fstream>
#include <memory>
#include <signal.h>

std::condition_variable stopNotify;
std::mutex stopGuard;
bool stop = false;

int main(int argc, const char **argv) {
	using std::string;
	
	if (argc > 2) {
		printf("Usage: %s <path to configuration JSON file = \"./config.json\">\n", argv[0]);
		return 0;
	}
	
	nlohmann::json config;
	std::shared_ptr<spdlog::logger> logger;
	bool result = true;

	{																//validate 'logs' folder
		using namespace std::experimental;

		const filesystem::path logPath("logs");
		bool createLogFolder = false;

		if (!filesystem::exists(logPath)) {
			createLogFolder = true;
		}
		else if (!filesystem::is_directory(logPath)) {
			if (!filesystem::remove(logPath)) {
				printf("Error removing existing '%s' intended for logging folder.\n", logPath.c_str());
				result = false;
			}
			else {
				createLogFolder = true;
			}
		}

		if (result && createLogFolder) {
			if (!filesystem::create_directory(logPath)) {
				printf("Error creating directory '%s' intended for logging folder.\n", logPath.c_str());
				result = false;
			}
		}
	}
	
	if (result) {													//setting up logging facility
		try {
			logger = spdlog::rotating_logger_mt<spdlog::async_factory>("log", "logs/main.log",
				1024 * 1024 * 8, 4);
			logger->flush_on(spdlog::level::trace);					//set level at 'main.h' instead
			logger->set_level(spdlog::level::trace);
			logger->set_pattern("%Y%m%dT%H%M%S,%e %L [%s:%#] %v");	//check 'tweakme.h' for config
			
			spdlog::set_default_logger(logger);						//we'll be using macro
		}
		catch (const spdlog::spdlog_ex &ex) {
			printf("Error initializing logging facility: %s.\n", ex.what());
			result = false;
		}
	}
	
	if (result) {													//read configuration JSON file
		const string configFilePath = ((argc >= 2) ? argv[1] : "./config.json");
		std::ifstream configStrm(configFilePath, std::ios_base::in);
		
		if (configStrm.fail()) {
			SPDLOG_CRITICAL("Error opening configuration JSON file at \"{}\".", configFilePath);
			result = false;
		}
		else {
			config = nlohmann::json::parse(configStrm, nullptr, false);

			if (!config.is_object()) {
				std::ostringstream strcf;
				
				strcf << configStrm.seekg(0).rdbuf();
				SPDLOG_DEBUG("Config JSON content:\n{}", strcf.str());
				
				SPDLOG_CRITICAL("Error parsing configuration JSON file.");
				result = false;
			}
		}
	}
	
	if (result) {													//Ctrl-C and 'kill' interrupt handler
		struct sigaction sigProp;
			
		sigemptyset(&sigProp.sa_mask);
		sigProp.sa_handler = [](int code) {
			SPDLOG_DEBUG("Got interrupt '{}'.", code);
			std::lock_guard<std::mutex> lock(stopGuard);
			stop = true;
			stopNotify.notify_all();
		};
		sigProp.sa_flags = 0;
		
		if (sigaction(SIGINT, &sigProp, nullptr)) {
			SPDLOG_CRITICAL("Error setting up interrupt handler.");
			result = false;
		}
	}
	
	if (result) {													//initialize subsystems
		nlohmann::json subConfig;
		
		if (!Private::JsonExtract(config, "bacnet_server", nlohmann::json::value_t::object, subConfig)) {
			Private::JsonLogTypeValidation(subConfig, "bacnet_server", nlohmann::json::value_t::object,
				logger, spdlog::level::critical);
			result = false;
		}
		else if (!hvac::BacServer::Init(subConfig)) {
			result = false;
		}
		
		if (!Private::JsonExtract(config, "device_manager", nlohmann::json::value_t::object, subConfig)) {
			Private::JsonLogTypeValidation(subConfig, "device_manager", nlohmann::json::value_t::object,
				logger, spdlog::level::critical);
			result = false;
		}
		else if (!hvac::DevManager::Init(subConfig)) {
			result = false;
		}
		
		if (!Private::JsonExtract(config, "site_simulator", nlohmann::json::value_t::object, subConfig)) {
			Private::JsonLogTypeValidation(subConfig, "site_simulator", nlohmann::json::value_t::object,
				logger, spdlog::level::critical);
			result = false;
		}
		else if (!hvac::SiteSim::Init(subConfig)) {
			result = false;
		}
		
		if (!Private::JsonExtract(config, "web_server", nlohmann::json::value_t::object, subConfig)) {
			Private::JsonLogTypeValidation(subConfig, "web_server", nlohmann::json::value_t::object,
				logger, spdlog::level::critical);
			result = false;
		}
		else if (!hvac::WebServer::Init(subConfig)) {
			result = false;
		}
	}
	
	if (result) {
		if (hvac::BacServer::Run() && hvac::WebServer::Run()) {
			std::unique_lock<std::mutex> lock(stopGuard);			//stop main thread here
			stopNotify.wait(lock, [](){ return stop; });
			
			hvac::BacServer::Stop();
			hvac::WebServer::Stop();
		}
	}
	
	return result ? 0 : 1;
}
