#include "main.h"
#include "BacServer.h"
#include "DevManager.h"

#include <Commons.h>

#include <bacnet/bacdef.h>
#include <bacnet/basic/object/bi.h>
#include <bacnet/basic/object/bo.h>
#include <bacnet/basic/object/device.h>
#include <bacnet/basic/services.h>
#include <bacnet/basic/tsm/tsm.h>
#include <bacnet/datalink/datalink.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>

//! Enforce \ref hvac::ValueWithPriority::Types variant type ordering via static_assert.
static_assert(std::is_same<BACNET_BINARY_PV,
	std::variant_alternative_t<0, hvac::ValueWithPriority::Types>>::value,
	"Expecting index 0 for 'BACNET_BINARY_PV' type.");

//! Enforce \ref hvac::ValueTypes variant type ordering via static_assert.
static_assert(std::is_same<std::string, std::variant_alternative_t<0, hvac::ValueTypes>>::value,
	"Expecting index 0 for 'string' type."
);
static_assert(std::is_same<bool, std::variant_alternative_t<1, hvac::ValueTypes>>::value,
	"Expecting index 1 for 'bool' type."
);
static_assert(std::is_same<hvac::ValueWithPriority,
	std::variant_alternative_t<2, hvac::ValueTypes>>::value,
	"Expecting index 2 for 'ValueWithPriority' type."
);

//! Checks (with static_assert) whether BACnet object present value type matches user variable type.
//! @tparam objType BACnet object type.
//! @tparam T User variable type.
//! @tparam doGet Whether it's called from getter function.
template<BACNET_OBJECT_TYPE objType, class T, bool doGet> constexpr static void CheckObjectValueType() {
	static_assert((objType != OBJECT_BINARY_INPUT) || std::is_same_v<T, bool>,
		"Object type requires boolean value.");
	
	if constexpr (doGet) {
		static_assert((objType != OBJECT_BINARY_OUTPUT) || std::is_same_v<T, bool>,
			"Object type requires boolean value.");
	}
	else {
		static_assert((objType != OBJECT_BINARY_OUTPUT) || std::is_same_v<T, hvac::ValueWithPriority>,
			"Object type requires value with priority.");
	}
	
	return;
}

//! Empty constructor.
hvac::ValueWithPriority::ValueWithPriority() {}

//! Constructor for BACNET_BINARY_PV type.
//! @param[in] val User value.
hvac::ValueWithPriority::ValueWithPriority(bool val) {
	value.emplace<BACNET_BINARY_PV>(val ? BINARY_ACTIVE : BINARY_INACTIVE);
	priority = 1;													//custom value will have highest priority
}

//! Deconstructor.
hvac::BacServer::~BacServer() {
	bip_cleanup();
	return;
}

//! Initializes BACnet server (as singleton object).
//! @param[in] config Configuration object.
//! @return True if server already initialized or initialized successfully.
bool hvac::BacServer::Init(const nlohmann::json &config) {
	bool result = true;
	
	if (!server) {
		try {
			server.reset(new BacServer(config));
		}
		catch (const std::exception &e) {
			SPDLOG_ERROR("Error initializing BACnet server: '{}'", e.what());
			result = false;
		}
	}
	
	return result;
}

//! Starts server process loop in separate thread.
//! @return False if server not initialized.
bool hvac::BacServer::Run() {
	bool result = true;
	
	if (!server) {
		SPDLOG_ERROR("BACnet server not initialized yet.");
		result = false;
	}
	else if (!server->thd.joinable()) {
		SPDLOG_INFO("BACnet server starting up.");
		server->stop = false;
		server->thd = std::thread(&BacServer::Process, server.get());
	}
	
	return result;
}

//! Stops server process loop running in separate thread.
//! @return False if server not initialized.
bool hvac::BacServer::Stop() {
	bool result = true;
	
	if (!server) {
		SPDLOG_ERROR("BACnet server not initialized yet.");
		result = false;
	}
	else if (server->thd.joinable()) {
		SPDLOG_INFO("BACnet server shutting down.");
		server->stop = true;
		server->thd.join();
	}
	
	return result;
}

//! Creates BACnet object.
//! @param[in] devId BACnet device instance ID. Currently ignored.
//! @param[in] objType BACnet object type.
//! @param[in] objId BACnet object instance ID. Must be lower than BACNET_MAX_INSTANCE.
//! @return True if BACnet object already exists or is created successfully.
bool hvac::BacServer::CreateObject(uint32_t, BACNET_OBJECT_TYPE objType, uint32_t objId) {
	bool result = false;
	
	if (Device_Valid_Object_Type(objType)) {
		switch (objType) {
		case OBJECT_BINARY_INPUT:
			result = Binary_Input_Create(objId);
			break;
		case OBJECT_BINARY_OUTPUT:
			result = Binary_Output_Create(objId);
			break;
		default:
			break;
		}
	}
	
	return result;
}

//! Deletes BACnet object.
//! @param[in] devId Target BACnet device instance ID. Currently ignored.
//! @param[in] objType Target BACnet object type.
//! @param[in] objId Target BACnet object instance ID.
//! @return True if target BACnet object doesn't exists or is deleted successfully.
bool hvac::BacServer::DeleteObject(uint32_t, BACNET_OBJECT_TYPE objType, uint32_t objId) {
	bool result = true;
	
	if (Device_Valid_Object_Type(objType)) {
		switch (objType) {
		case OBJECT_BINARY_INPUT:
			result = Binary_Input_Delete(objId);
			break;
		case OBJECT_BINARY_OUTPUT:
			result = Binary_Output_Delete(objId);
			break;
		default:
			break;
		}
	}
	
	return result;
}

//! Getter for BACnet object present value.
//! @tparam objType Target BACnet object type.
//! @tparam T User variable type. Deductible from \b val.
//! @param[in] devId Target BACnet device instance ID. Currently ignored.
//! @param[in] objId Target BACnet object instance ID.
//! @param[out] val User variable reference for output. Its type must match target object present value type.
//! @return True if target BACnet object exists.
template<BACNET_OBJECT_TYPE objType, class T> bool hvac::BacServer::GetObjectValue(uint32_t, uint32_t objId,
T &val) {
	CheckObjectValueType<objType, T, true>();
	
	bool result = false;
	
	if (Device_Valid_Object_Id(objType, objId)) {
		switch (objType) {
		case OBJECT_BINARY_INPUT:
			val = (Binary_Input_Present_Value(objId) == BINARY_ACTIVE);
			result = true;
			break;
		case OBJECT_BINARY_OUTPUT:
			val = (Binary_Output_Present_Value(objId) == BINARY_ACTIVE);
			result = true;
			break;
		default:
			break;
		}
	}
	
	return result;
}

//! Setter for BACnet object present value.
//! @tparam objType Target BACnet object type.
//! @tparam T User variable type. Deductible from \b val.
//! @param[in] devId Target BACnet device instance ID. Currently ignored.
//! @param[in] objId Target BACnet object instance ID.
//! @param[out] val User variable reference for output. Its type must match target object present value type.
//! @return True if target BACnet object exists and its value is set successfully.
template<BACNET_OBJECT_TYPE objType, class T> bool hvac::BacServer::SetObjectValue(uint32_t, uint32_t objId,
const T &val) {
	CheckObjectValueType<objType, T, false>();
	
	bool result;
	
	if constexpr (objType == OBJECT_BINARY_INPUT) {
		result = Binary_Input_Present_Value_Set(objId, val ? BINARY_ACTIVE : BINARY_INACTIVE);
	}
	else if constexpr (objType == OBJECT_BINARY_OUTPUT) {
		if (std::holds_alternative<BACNET_BINARY_PV>(val.value)) {
			result = Binary_Output_Present_Value_Set(objId, std::get<BACNET_BINARY_PV>(val.value),
				val.priority);
		}
		else {
			result = false;
		}
	}
	else {
		result = false;
	}
	
	return result;
}

//! Bridge from BACnet stack to device manager to get BACnet object present value.
//! @tparam objType Target BACnet object type.
//! @tparam T New BACnet object value type. Deductible from \b val.
//! @param[in] devId Target BACnet device instance ID.
//! @param[in] objId Target BACnet object instance ID.
//! @param[out] val New BACnet object value. Its type must match target object present value type. Only being
//!					modified if function returns true.
//! @param[out] errClass BACnet error class. Only being modified when there's error.
//! @param[out] errCode BACnet error code. Only being modified when there's error.
//! @return True if object value change operation is allowed.
template<BACNET_OBJECT_TYPE objType, class T> bool hvac::BacServer::GetObjectValueBS(uint32_t devId,
uint32_t objId, T &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode) {
	CheckObjectValueType<objType, T, true>();
	
	ValueTypes tmpVal(val);
	uint32_t status = DevManager::GetConnectorValue(devId, objType, objId, tmpVal);
	bool result;
	
	if (status == 200u) {
		if (std::holds_alternative<T>(tmpVal)) {
			val = std::get<T>(tmpVal);
			result = true;
		}
		else {
			status = 503u;
		}
	}
	
	if (status != 200u) {
		result = false;
		
		switch (status) {
		case 403u:
			errClass = ERROR_CLASS_PROPERTY;
			errCode = ERROR_CODE_WRITE_ACCESS_DENIED;
			break;
		case 404u:
			errClass = ERROR_CLASS_OBJECT;
			errCode = ERROR_CODE_UNKNOWN_OBJECT;
			break;
		case 503u:
			errClass = ERROR_CLASS_DEVICE;
			errCode = ERROR_CODE_OPERATIONAL_PROBLEM;
			break;
		default:
			errClass = ERROR_CLASS_OBJECT;
			errCode = ERROR_CODE_OTHER;
			break;
		}
	}
	
	return result;
}

//! Bridge from BACnet stack to device manager to set BACnet object present value.
//! @tparam objType Target BACnet object type.
//! @tparam T New BACnet object value type. Deductible from \b val.
//! @param[in] devId Target BACnet device instance ID.
//! @param[in] objId Target BACnet object instance ID.
//! @param[in] val New BACnet object value. Its type must match target object present value type.
//! @param[out] errClass BACnet error class. Modified when there's error.
//! @param[out] errCode BACnet error code. Modified when there's error.
//! @return True if object value change operation is allowed.
template<BACNET_OBJECT_TYPE objType, class T> bool hvac::BacServer::SetObjectValueBS(uint32_t devId,
uint32_t objId, const T &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode) {
	CheckObjectValueType<objType, T, false>();
	
	const uint32_t status = DevManager::SetConnectorValue(devId, objType, objId, val);
	bool result;
	
	if (status == 200u) {
		result = true;
	}
	else {
		result = false;
		
		switch (status) {
		case 403u:
			errClass = ERROR_CLASS_PROPERTY;
			errCode = ERROR_CODE_WRITE_ACCESS_DENIED;
			break;
		case 404u:
			errClass = ERROR_CLASS_OBJECT;
			errCode = ERROR_CODE_UNKNOWN_OBJECT;
			break;
		case 503u:
			errClass = ERROR_CLASS_DEVICE;
			errCode = ERROR_CODE_OPERATIONAL_PROBLEM;
			break;
		default:
			errClass = ERROR_CLASS_OBJECT;
			errCode = ERROR_CODE_OTHER;
			break;
		}
	}
	
	return result;
}

//! Constructor.
//! @param config Configuration object.
//! @throw invalid_argument If configuration JSON type is not object.
//!							If error setting device ID/name and network interface name/port.
hvac::BacServer::BacServer(const nlohmann::json &config) {
	if (!config.is_object()) {
		throw std::invalid_argument("Invalid configuration JSON type.");
	}
	
	nlohmann::json jsonVal;
	std::string deviceName = "HVAC Device 0", nicName = "eth0";
	uint32_t deviceId = 0, nicPort = 47808;
	
	address_init();
	Device_Init(NULL);
	apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
	apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_HAS, handler_who_has);
	apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
	apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, handler_read_property_multiple);
	apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);
	apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROP_MULTIPLE, handler_write_property_multiple);
	apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
	
	if (!Private::JsonExtract(config, "device_id", nlohmann::json::value_t::number_unsigned, jsonVal)) {
		SPDLOG_WARN("Missing or invalid 'device_id' configuration, defaulting to '{}'.", deviceId);
	}
	else {
		deviceId = jsonVal.get<uint32_t>();
	}
	if (!Device_Set_Object_Instance_Number(deviceId)) {
		throw std::invalid_argument("Error setting BACnet device ID to '" + std::to_string(deviceId) + "'.");
	}
	
	if (!Private::JsonExtract(config, "device_name", nlohmann::json::value_t::string, jsonVal)) {
		SPDLOG_WARN("Missing or invalid 'device_name' configuration, defaulting to '{}'.", deviceName);
	}
	else {
		deviceName = jsonVal.get<std::string>();
	}
	if (!Device_Object_Name_ANSI_Init(deviceName.data())) {
		throw std::invalid_argument("Error setting BACnet device name to '" + deviceName + "'.");
	}
	
	if (!Private::JsonExtract(config, "port", nlohmann::json::value_t::number_unsigned, jsonVal)) {
		SPDLOG_WARN("Missing or invalid 'port' configuration, defaulting to '{}'.", nicPort);
	}
	else {
		nicPort = jsonVal.get<uint32_t>();
	}
	bip_set_port(nicPort);
	
	if (!Private::JsonExtract(config, "nic_name", nlohmann::json::value_t::string, jsonVal)) {
		SPDLOG_WARN("Missing or invalid 'nic_name' configuration, defaulting to '{}'.", nicName);
	}
	else {
		nicName = jsonVal.get<std::string>().data();
	}
	if (!bip_init(nicName.data())) {
		throw std::invalid_argument("Error initializing BACnet IP service with NIC '" + nicName + "'.");
	}
	
	return;
}

//! Server process that is intended to run on separate thread.
void hvac::BacServer::Process() {
	const std::unique_ptr<uint8_t> recvBuf(new uint8_t[MAX_MPDU]);
	const std::unique_ptr<BACNET_ADDRESS> recvSrc(new BACNET_ADDRESS());
	time_t prevSec = time(NULL);
	uint32_t addrElapsedSec = 0;
	
	{																//for reporting purpose only
		BACNET_CHARACTER_STRING bacName;
		BACNET_IP_ADDRESS bacAddr;
		
		if (Device_Object_Name(Device_Object_Instance_Number(), &bacName)) {
			bip_get_addr(&bacAddr);
			
			SPDLOG_INFO("BACnet server with device ID:'{}' name:'{}' listening at '{}.{}.{}.{}:{}'.",
				Device_Object_Instance_Number(), bacName.value, bacAddr.address[0], bacAddr.address[1],
				bacAddr.address[2], bacAddr.address[3], bacAddr.port);
		}
	}
	
	while (!stop) {
		const time_t curSec = time(NULL);
		const uint32_t elapsedSec = (uint32_t)(curSec - prevSec);
		const uint16_t recvLen = bip_receive(recvSrc.get(), recvBuf.get(), MAX_MPDU, 10);
		
		if (recvLen) {												//returns 0 bytes on timeout
			npdu_handler(recvSrc.get(), recvBuf.get(), recvLen);
		}
		
		if (elapsedSec) {											//at least one second has passed
			prevSec = curSec;
			tsm_timer_milliseconds(elapsedSec * 1000u);
		}
		
		addrElapsedSec += elapsedSec;
		if (addrElapsedSec >= 60) {									//manage cached address
			address_cache_timer(addrElapsedSec);
			addrElapsedSec = 0;
		}
	}
	
	SPDLOG_INFO("BACnet server shut down.");
	
	return;
}

std::unique_ptr<hvac::BacServer> hvac::BacServer::server;

//explicit template instantiation
template bool hvac::BacServer::GetObjectValue<OBJECT_BINARY_INPUT>(uint32_t devId, uint32_t objId, bool &val);
template bool hvac::BacServer::GetObjectValue<OBJECT_BINARY_OUTPUT>(uint32_t devId, uint32_t objId,
	bool &val);
template bool hvac::BacServer::SetObjectValue<OBJECT_BINARY_INPUT>(uint32_t devId, uint32_t objId,
	const bool &val);
template bool hvac::BacServer::SetObjectValue<OBJECT_BINARY_OUTPUT>(uint32_t devId, uint32_t objId,
	const hvac::ValueWithPriority &val);
template bool hvac::BacServer::GetObjectValueBS<OBJECT_BINARY_INPUT>(uint32_t devId, uint32_t objId,
	bool &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode);
template bool hvac::BacServer::GetObjectValueBS<OBJECT_BINARY_OUTPUT>(uint32_t devId, uint32_t objId,
	bool &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode);
template bool hvac::BacServer::SetObjectValueBS<OBJECT_BINARY_INPUT>(uint32_t devId, uint32_t objId,
	const bool &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode);
template bool hvac::BacServer::SetObjectValueBS<OBJECT_BINARY_OUTPUT>(uint32_t devId, uint32_t objId,
	const hvac::ValueWithPriority &val, BACNET_ERROR_CLASS &errClass, BACNET_ERROR_CODE &errCode);
