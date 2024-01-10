#include "main.h"
#include "DevManager.h"
#include "WebServer.h"

#include <Commons.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

struct ClientData {};

//! Initializes WebSocket/REST API server (as singleton object).
//! @param[in] config Configuration object.
//! @return True if server already initialized or initialized successfully.
bool hvac::WebServer::Init(const nlohmann::json &config) {
	bool result = true;
	
	if (!server) {
		try {
			server.reset(new WebServer(config));
		}
		catch (const std::exception &e) {
			SPDLOG_ERROR("Error initializing web server: '{}'", e.what());
			result = false;
		}
	}
	
	return result;
}

//! Starts server process loop in separate thread.
//! @return False if server not initialized.
bool hvac::WebServer::Run() {
	bool result = true;
	
	if (!server) {
		SPDLOG_ERROR("Web server not initialized yet.");
		result = false;
	}
	else if (!server->thdMain.joinable()) {
		SPDLOG_INFO("Web server starting up.");
		server->stop = false;
		server->thdMain = std::thread(&WebServer::Process, server.get());
		server->thdPing = std::thread(&WebServer::PingClients, server.get());
	}
	
	return result;
}

//! Stops server process loop running in separate thread.
//! @return False if server not initialized.
bool hvac::WebServer::Stop() {
	bool result = true;
	
	if (!server) {
		SPDLOG_ERROR("Web server not initialized yet.");
		result = false;
	}
	else {
		if (server->thdPing.joinable()) {
			SPDLOG_INFO("Web server stopping client pinger.");
			server->stop = true;
			server->thdPing.join();
		}
		
		if (server->thdMain.joinable()) {
			{
				std::lock_guard<decltype(wsSocksGuard)> lock(server->wsSocksGuard);
				
				if (!server->wsSocks.empty()) {
					SPDLOG_INFO("Web server closing still connected socket.");
					
					for (auto wsSock : server->wsSocks) {
						if (server->useSsl) {
							((uWS::WebSocket<true, true>*) wsSock)->end();
						}
						else {
							((uWS::WebSocket<false, true>*) wsSock)->end();
						}
					}
				}
			}
			
			if (server->token) {
				SPDLOG_INFO("Web server closing listening socket.");
				us_listen_socket_close(server->useSsl, server->token);
				server->token = nullptr;
			}
			
			server->thdMain.join();
		}
	}
	
	return result;
}

//! Sends message to all clients connected to server.
//! @param[in] msg Message to be sent.
//! @param[in] opcode WebSocket protocol transaction opcode.
//! @return False if server not initialized.
bool hvac::WebServer::SendMessage(std::string_view msg, uWS::OpCode opcode) {
	bool result = true;
	
	if (!server) {
		SPDLOG_ERROR("Web server not initialized yet.");
		result = false;
	}
	else {
		std::lock_guard<decltype(wsSocksGuard)> lock(server->wsSocksGuard);
		
		if (!server->wsSocks.empty()) {
			for (auto wsSock : server->wsSocks) {
				if (server->useSsl) {
					if (!((uWS::WebSocket<true, true>*) wsSock)->send(msg, opcode, true)) {
						SPDLOG_WARN("Failed sending WebSocket message '{}'.", msg);
					}
				}
				else if (!((uWS::WebSocket<false, true>*) wsSock)->send(msg, opcode, true)) {
					SPDLOG_WARN("Failed sending WebSocket message '{}'.", msg);
				}
			}
		}
	}
	
	return result;
}

//! Constructor.
//! @param config Configuration object.
//! @throw invalid_argument If configuration JSON type is not object.
//!							If SSL is enabled but related configuration is invalid.
hvac::WebServer::WebServer(const nlohmann::json &config) {
	if (!config.is_object()) {
		throw std::invalid_argument("Invalid configuration JSON type.");
	}
	
	nlohmann::json jsonVal;
	
	if (!Private::JsonExtract(config, "use_ssl", nlohmann::json::value_t::boolean, jsonVal)) {
		SPDLOG_WARN("Missing or invalid 'use_ssl' configuration, defaulting to false.");
		useSsl = false;
	}
	else {
		useSsl = jsonVal.get<bool>();
	}
	
	if (useSsl) {
		auto fx = [&config, &jsonVal](const std::string &key, std::string &val) {
			if (!Private::JsonExtract(config, key, nlohmann::json::value_t::string, jsonVal)) {
				throw std::invalid_argument("Missing or invalid '" + key + "' configuration.");
			}
			else {
				val = jsonVal.get<std::string>();
			}
		};
		
		fx("cert_key_path", certKeyPath);
		fx("cert_pass", certPass);
		fx("cert_path", certPath);
	}
	
	if (!Private::JsonExtract(config, "www_root", nlohmann::json::value_t::string, jsonVal)) {
		throw std::invalid_argument("Missing or invalid 'www_root' configuration.");
	}
	else {
		std::unique_ptr<char> testPath(realpath(jsonVal.get<std::string>().data(), nullptr));
		
		if (testPath) {
			wwwPath = testPath.get();
		}
		else {
			throw std::invalid_argument("'www_root' configuration error: " +
				std::string(std::strerror(errno)));
		}
	}
	
	if (!Private::JsonExtract(config, "port", nlohmann::json::value_t::number_unsigned, jsonVal)) {
		SPDLOG_WARN("Missing or invalid 'port' configuration, defaulting to 9001.");
		port = 9001;
	}
	else {
		port = jsonVal.get<uint32_t>();
	}
	
	return;
}

//! Helper function to get/read website file in wwwPath. Requesting root path ('/') will return '/index.html'.
//! @param[in] filePath Partial target file path (wwwPath will be prepended later).
//! @param[out] headers Headers (e.g. MIME type) to be written as part of response.
//! @param[out] resp Target file content or HTTP status code if error.
//! @return True if target file exists and file is read successfully.
bool hvac::WebServer::GetWwwFile(std::string_view filePath,
std::unordered_map<std::string, std::string> &headers, std::string &resp) const {
	const std::string_view &chkFilePath = (filePath == "/") ? "/index.html" : filePath;
	const std::unique_ptr<char> validPath(realpath((wwwPath + std::string(chkFilePath.data(),
		chkFilePath.size())).data(), nullptr));
	bool result = true;

	//check whether target file path valid or not, then whether file is within 'wwwPath' or not.
	if ((validPath) && (std::strstr(validPath.get(), wwwPath.c_str()) == validPath.get())) {
		const std::filesystem::path &ext = std::filesystem::path(validPath.get()).extension();
			
		if (ext == ".html") {
			headers.insert_or_assign("Content-Type", "text/html");
		}
		else if (ext == ".js") {
			headers.insert_or_assign("Content-Type", "application/javascript");
		}
		else if (ext == ".css") {
			headers.insert_or_assign("Content-Type", "text/css");
		}
		else if (ext == ".ico") {
			headers.insert_or_assign("Content-Type", "image/x-icon");
		}
		else {
			SPDLOG_WARN("Attempt to open unhandled file type: '{}'.", validPath.get());
			result = false;
		}
		
		if (result) {
			std::ifstream fileStrm(validPath.get(), std::ios_base::in | std::ios::ate);
		
			if (fileStrm.fail()) {
				SPDLOG_DEBUG("Error opening target file: '{}'.", validPath.get());
				result = false;
			}
			else {
				const std::fpos fileSz = fileStrm.tellg();
				resp.assign(fileSz, '\0');
				fileStrm.seekg(0).read(resp.data(), fileSz);
			}
		}
	}
	else {
		SPDLOG_WARN("Invalid target file path: '{}'.", chkFilePath);
		result = false;
	}
	
	if (!result) {
		resp = "404 Not Found";
	}

	return result;
}

//! Server process that is intended to run on separate thread.
void hvac::WebServer::Process() {
	auto fxStartServer = [this](auto app) {
		app.get("/*", [this](auto *res, uWS::HttpRequest *req) {
			std::unordered_map<std::string, std::string> headers;
			std::string resp;
			
			if (GetWwwFile(req->getUrl(), headers, resp)) {
				for (const auto &header : headers) {
					res->writeHeader(header.first, header.second);
				}
				res->end(resp);
			}
			else {
				res->writeStatus(resp)->end();
			}
		}).post("/api", [](auto *res, uWS::HttpRequest*) {
			std::string dataFull;
			
			res->onAborted([]() {									//need to add this lest it'll crash
				return;
			});
			res->onData([res, &dataFull](std::string_view dataPartial, bool done) {
				dataFull.append(dataPartial);
				
				if (done) {
					nlohmann::json resp;
					
					if (DevManager::RestApiHandler(dataFull, resp)) {
						res->end(resp.dump(1, '\t', false, nlohmann::json::error_handler_t::replace));
					}
					else {
						res->writeStatus(resp.get<std::string_view>())->end();
					}
				}
				else {
					res->writeContinue()->end();
				}
			});
		}).template ws<ClientData>("/*", {
			.compression = uWS::SHARED_COMPRESSOR,
			.maxPayloadLength = 16 * 1024 * 1024,
			.idleTimeout = 10,
			.maxBackpressure = 1 * 1024 * 1024,
			.upgrade = nullptr,
			.open = [this](auto *ws) {
				SPDLOG_TRACE("WebSocket connected.");
				
				std::lock_guard<decltype(wsSocksGuard)> lock(wsSocksGuard);
				const typename decltype(wsSocks)::iterator &wsFind =
					std::find(wsSocks.begin(), wsSocks.end(), ws);
				nlohmann::json resp;
				
				if (wsFind != wsSocks.end()) {
					SPDLOG_WARN("WebSocket '{}' already in list.", ws->getRemoteAddressAsText());
				}
				else {
					SPDLOG_TRACE("WebSocket entry '{}' inserted.", ws->getRemoteAddressAsText());
					wsSocks.emplace_back(ws);
				}
				
				if (DevManager::RestApiHandler(R"([{
					"connector_value": {},
					"type": "read"
				}])", resp)) {
					nlohmann::json jsonVal;
					
					if (Private::RestExtractResponse(resp, "connector_value", "read", 200,
					nlohmann::json::value_t::object, jsonVal)) {
						ws->send(jsonVal.dump(1, '\t', false, nlohmann::json::error_handler_t::replace),
							uWS::OpCode::TEXT, true);
					}
				}
			},
			.message = [](auto*, std::string_view, uWS::OpCode) {},
			.drain = [](auto *ws) {
				SPDLOG_WARN("WebSocket draining {} bytes.", ws->getBufferedAmount());
			},
			.ping = [](auto*) {},
			.pong = [](auto*) {},
			.close = [this](auto *ws, int code, std::string_view msg) {
				SPDLOG_DEBUG("WebSocket disconnected with code '{}': {}", code, msg);
				
				std::lock_guard<decltype(wsSocksGuard)> lock(wsSocksGuard);
				const typename decltype(wsSocks)::iterator &wsFind =
					std::find(wsSocks.begin(), wsSocks.end(), ws);
				
				if (wsFind == wsSocks.end()) {
					SPDLOG_WARN("WebSocket '{}' not found from list.", ws->getRemoteAddressAsText());
				}
				else {
					SPDLOG_TRACE("WebSocket entry '{}' erased.", ws->getRemoteAddressAsText());
					wsSocks.erase(wsFind);
				}
				
				return;
			}
		}).listen(port, [this](auto *token) {
			if (token) {
				SPDLOG_INFO("Web server listening at port '{}'.", port);
				this->token = token;
			}
		}).run();
	};
	
	if (useSsl) {
		fxStartServer(uWS::TemplatedApp<true>({
			.key_file_name = certKeyPath.data(),
			.cert_file_name = certPath.data(),
			.passphrase = certPass.data(),
			.dh_params_file_name = nullptr,
			.ca_file_name = nullptr,
			.ssl_prefer_low_memory_usage = 0
		}));
	}
	else {
		fxStartServer(uWS::TemplatedApp<false>());
	}
	
	SPDLOG_INFO("Web server shut down.");
	
	return;
}

//! Helper function to periodically send ping to all connected clients.
void hvac::WebServer::PingClients() {
	while (!stop) {
		std::this_thread::sleep_for(std::chrono::seconds(4));
		if (!hvac::WebServer::SendMessage("", uWS::OpCode::PING)) {
			SPDLOG_DEBUG("Error sending ping message.");
		}
	}
	
	return;
}

std::unique_ptr<hvac::WebServer> hvac::WebServer::server;
