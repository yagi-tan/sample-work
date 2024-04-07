#include "main.h"
#include "proc.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <signal.h>

#include <cstdio>
#include <memory>
#include <vector>

static const char *device = NULL, *driver = NULL;
static usb_device_speed speed = USB_SPEED_UNKNOWN;

bool initSys() {
	printf("%s: Starting program. Initializing...\n", __FUNCTION__);
	
	try {
		std::vector<spdlog::sink_ptr> sinks;
		spdlog::sink_ptr sink;
		
		sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
		sink->set_level(spdlog::level::info);
		sink->set_pattern("%L: %v");								//check 'tweakme.h' for config
		sinks.push_back(sink);
		
		sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/main.log", 1024 * 1024 * 8, 4);
		sink->set_level(spdlog::level::trace);
		sink->set_pattern("%Y%m%dT%H%M%S,%e %L [%s:%#] %v");		//check 'tweakme.h' for config
		sinks.push_back(sink);
		
		std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>("log", sinks.begin(),
			sinks.end());
		logger->flush_on(spdlog::level::trace);						//set level at 'main.h' instead
		logger->set_level(spdlog::level::trace);
		spdlog::set_default_logger(logger);							//we'll be using macro
	}
	catch (const spdlog::spdlog_ex &ex) {
		printf("Error initializing logging facility: %s.\n", ex.what());
		return false;
	}
	
	//Ctrl-C and 'kill' interrupt handler
	//**********************************************************************************
	struct sigaction sigProp;
	
	sigemptyset(&sigProp.sa_mask);
	sigProp.sa_handler = [](int code) {
		SPDLOG_WARN("Got interrupt '{}'.", code);
		stopProc();
	};
	sigProp.sa_flags = 0;
	
	if (sigaction(SIGINT, &sigProp, nullptr)) {
		SPDLOG_CRITICAL("Error setting up interrupt handler.");
		return false;
	}
	//**********************************************************************************
	
	return true;
}

void exitSys() {
	SPDLOG_INFO("{}: Program ended. Exiting...", __FUNCTION__);
	
	spdlog::shutdown();
}

//! Helper function to parse command-line arguments.
bool parseArgs(int argc, char **args) {
	int opt;
	bool result = true;
	
	while ((opt = getopt(argc, args, "e:r:s")) != -1) {
		switch (opt) {
			case 'e':
				device = optarg;
				break;
			case 'r':
				driver = optarg;
				break;
			case 's':
				if (!strcasecmp(optarg, "low")) {
					speed = USB_SPEED_LOW;
				}
				else if (!strcasecmp(optarg, "full")) {
					speed = USB_SPEED_FULL;
				}
				else if (!strcasecmp(optarg, "high")) {
					speed = USB_SPEED_HIGH;
				}
				else {
					result = false;
				}
				break;
			default:
				result = false;
				break;
		}
	}
	
	if (result) {
		if (!device) {
			device = "dummy_udc.0";
			printf("Missing '-e' argument, defaulting to '%s'.\n", device);
		}
		
		if (!driver) {
			driver = "dummy_udc";
			printf("Missing '-r' argument, defaulting to '%s'.\n", driver);
		}
		
		if (speed == USB_SPEED_UNKNOWN) {
			speed = USB_SPEED_FULL;
			printf("Missing '-s' argument, defaulting to 'full'.\n");
		}
	}
	
	if (!result) {
		printf("Usage:\t%s ", args[0]);
		printf("<-e UDC device [dummy_udc.0]> <-r UDC driver [dummy_udc]> <-s USB speed [full]>\n");
	}
	
	return result;
}

int main(int argc, char **args) {
	if (parseArgs(argc, args)) {
		if (initSys()) {
			SPDLOG_INFO("Within user main().");
			
			startProc(device, driver, speed);
		}
		
		exitSys();
	}
	
	return 0;
}
