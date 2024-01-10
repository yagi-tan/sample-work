#ifndef	WEBSERVER_H
#define	WEBSERVER_H

#include <nlohmann/json.hpp>
#include <uWebSockets/App.h>

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hvac {
	//! REST/WebSocket web server.
	class WebServer {
	public:
		static bool Init(const nlohmann::json &config);
		static bool Run();
		static bool Stop();
		
		static bool SendMessage(std::string_view msg, uWS::OpCode opcode = uWS::OpCode::TEXT);
	private:
		WebServer(const nlohmann::json &config);
		
		bool GetWwwFile(std::string_view filePath,
			std::unordered_map<std::string, std::string> &headers, std::string &resp) const;
		void PingClients();
		void Process();
		
		us_listen_socket_t *token = nullptr;
		std::vector<void*> wsSocks;
		std::recursive_mutex wsSocksGuard;
		std::thread thdMain, thdPing;
		std::string certKeyPath, certPass, certPath, wwwPath;
		int port;
		bool stop, useSsl;
		
		static std::unique_ptr<WebServer> server;
	};
}

#endif
