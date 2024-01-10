#ifndef	BACSERVER_H
#define	BACSERVER_H

#include <bacnet/bacenum.h>

#include <nlohmann/json.hpp>

#include <memory>
#include <thread>
#include <variant>

namespace hvac {
	//! Helper data structure to deal with object with multiple priorities (AO, BO etc.).
	struct ValueWithPriority {
		typedef std::variant<BACNET_BINARY_PV> Types;
		
		ValueWithPriority();
		explicit ValueWithPriority(bool val);
		
		//! Object value associated with priority.
		Types value;
		//! Priority for new value.
		unsigned priority;
	};
	
	//! Possible value types for various BACnet object and connector types.
	typedef std::variant<std::string, bool, ValueWithPriority> ValueTypes;
	
	//! BACnet object population and server. Only supports single BACnet device (for now).
	class BacServer {
	public:
		~BacServer();
		
		static bool Init(const nlohmann::json &config);
		static bool Run();
		static bool Stop();
		
		static bool CreateObject(uint32_t devId, BACNET_OBJECT_TYPE objType, uint32_t objId);
		static bool DeleteObject(uint32_t devId, BACNET_OBJECT_TYPE objType, uint32_t objId);
		template<BACNET_OBJECT_TYPE objType, class T> static bool GetObjectValue(uint32_t devId,
			uint32_t objId, T &val);
		template<BACNET_OBJECT_TYPE objType, class T> static bool SetObjectValue(uint32_t devId,
			uint32_t objId, const T &val);
		
		template<BACNET_OBJECT_TYPE objType, class T> static bool GetObjectValueBS(uint32_t devId,
			uint32_t objId, T &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode);
		template<BACNET_OBJECT_TYPE objType, class T> static bool SetObjectValueBS(uint32_t devId,
			uint32_t objId, const T &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode);
	private:
		BacServer(const nlohmann::json &config);
		
		void Process();
		
		std::thread thd;
		bool stop;
		
		static std::unique_ptr<BacServer> server;
	};
}

#endif
