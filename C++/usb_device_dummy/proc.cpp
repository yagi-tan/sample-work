#include "generator.h"
#include "main.h"
#include "proc.h"

#include <fcntl.h>
#include <linux/usb/raw_gadget.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <list>
#include <string_view>
#include <thread>
#include <utility>

#define USB_ID_VENDOR			0x0627u								//!< USB vendor ID
#define USB_ID_PRODUCT			0x0001u								//!< USB product ID
#define USB_MAX_POWER			50u									//!< 50 * 2mA = 100mA
#define USB_SELECT_CONFIG		1u									//!< The only configuration available.
#define USB_SELECT_INTERFACE	0									//!< The only interface available.
#define DEVICE_VERSION			0x0100u								//!< This device version.

enum class StringId : uint8_t {										//!< Descriptor string IDs.
	LANG_IDS = 0u,
	MANUFACTURER,
	PRODUCT,
	SERIAL,
	CONFIG,
	INTERFACE
};

//! Maximum data size in \ref usb_raw_ep_io.
#define MAX_IO_DATA_LEN			256u
//! Maximum packet size for any endpoint types, in bytes.
#define MAX_PACKET_SIZE			64u
static_assert(!(MAX_IO_DATA_LEN % MAX_PACKET_SIZE), "IO data length must be multiples of packet size.");

//! Thread to perform bulk-in transfer for single logic analyser channel readings.
class ChannelThd {
public:
	//! Constructor.
	//! @param[in] fd File descriptor for '/dev/raw-gadget'.
	//! @param[in] idx Channel index.
	//! @param[in] epHandle Handle to enabled endpoint associated with the channel.
	ChannelThd(int fd, __u8 idx, __u16 epHandle) noexcept : idx(idx), epHandle(epHandle), fd(fd) {
		run = false;
		genSz = 0u;
	}
	
	virtual ~ChannelThd() {
		SPDLOG_WARN("Channel {} thread removed.", idx);
	}
	
	//! Process function.
	void proc() {
		std::unique_ptr<uint8_t[]> ioRaw(new (std::nothrow) uint8_t[sizeof(usb_raw_ep_io) + MAX_IO_DATA_LEN]);
		auto io = reinterpret_cast<usb_raw_ep_io*>(ioRaw.get());
		std::deque<uint8_t> data;
		
		if (!ioRaw) {
			SPDLOG_ERROR("Error allocating 'usb_raw_ep_io' object for channel {} thread.", idx);
			return;
		}
		
		io->ep = epHandle;
		io->flags = 0u;
		
		//! Helper function to send data to bulk-in endpoint.
		auto fx = [this, io]() -> bool {
			SPDLOG_TRACE("Channel {} bulk-in do write.", idx);
			
			const int ioQt = ioctl(fd, USB_RAW_IOCTL_EP_WRITE, io);
			
			if (ioQt < 0) [[unlikely]] {
				SPDLOG_ERROR("Channel {} error bulk-in writing: {}", idx, std::strerror(errno));
				return false;
			}
			
			if (static_cast<__u32>(ioQt) != io->length) [[unlikely]] {
				SPDLOG_ERROR("Channel {} error bulk-in writing data mismatch expected:{} actual:{}", idx,
					io->length, ioQt);
				return false;
			}
			
			SPDLOG_TRACE("Channel {} bulk-in wrote {} byte(s).", idx, ioQt);
			
			return true;
		};
		
		run = true;
		while (run) {
			genFlag.wait(false);
			if (!genSz) {
				continue;
			}
			
			const size_t genSzRef = genSz;
			bool result = generateData(idx, data, genSz);
			size_t dataSent = 0u;
			
			genSz = std::min(data.size(), genSz);
			while (result && genSz) {
				const size_t dataSend = std::min(genSz, (size_t) MAX_IO_DATA_LEN);
				
				std::copy_n(data.begin() + dataSent, dataSend, io->data);
				io->length = dataSend;
				
				if (fx()) [[likely]] {
					genSz -= dataSend;
					dataSent += dataSend;
				}
				else {
					result = false;
				}
			}
			
			//in case sent data size matches packet boundary while didn't exactly fulfil request
			if (result && (dataSent < genSzRef) && !(dataSent % MAX_PACKET_SIZE)) {
				io->length = 0u;									//send short packet
				result = fx();
			}
			
			if (!result && ioctl(fd, USB_RAW_IOCTL_EP_SET_HALT, epHandle)) {
				SPDLOG_ERROR("Channel {} error halting endpoint: {}.", idx, std::strerror(errno));
			}
			
			SPDLOG_DEBUG("Channel {} done processing {}-byte(s) request.", idx, genSzRef);
			
			data.clear();
			genSz = 0u;
			genFlag.clear();
			genFlag.notify_one();									//in case process is stopping
		}
	}
	
	//! Inform channel to send data.
	//! @param[in,out] maxSz Maximum data size to be sent, in bytes. x &gt; 0. To be set with pending request
	//!						 leftover data size, or newly requested data size as new request.
	//! @return True if no error has occurred.
	bool getData(size_t &maxSz) {
		if (!maxSz) {
			SPDLOG_ERROR("User requested 0-byte data from channel {}.", idx);
			return false;
		}
		
		if (genFlag.test()) {
			SPDLOG_WARN("Channel {} still handling request with {} byte(s) left.", idx, genSz);
			maxSz = genSz;
		}
		else {
			genSz = maxSz;
			genFlag.test_and_set();
			genFlag.notify_one();
		}
		
		return true;
	}
	
	//! Getter for thread status.
	//! @return True if thread is still running.
	bool isRunning() const { return run; }
	
	//! Stops process.
	void stopProc() {
		genFlag.wait(true);											//in case process is ongoing
		run = false;
		genFlag.test_and_set();
		genFlag.notify_one();
	}
	
	const __u8 idx;
	const __u16 epHandle;
	
private:
	const int fd;
	bool run;
	
	std::atomic_flag genFlag;
	size_t genSz;
};

static std::list<std::pair<int, usb_raw_ep_info>> epsInfo;			//!< &lt;enabled endpoint handle, info&gt;
//! Thread handling bulk-in requests. channel index -&gt; &lt;channel object, thread&gt;.
static std::map<__u8, std::pair<std::unique_ptr<ChannelThd>, std::thread>> channelThds;
static bool run = true;

bool enableEps(int fd);												//forward declaration

//! Helper function to manage channel threads (removal of dead thread before addition).
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] idx Channel index.
//! @return True if channel thread already running or added successfully.
bool channelThdAdd(int fd, __u8 idx) {
	auto iter = channelThds.find(idx);
	
	if (iter != channelThds.end()) {							//search for existing channel
		if (!iter->second.first->isRunning()) {
			SPDLOG_WARN("Channel {} thread found dead. Restarting.", idx);
			
			iter->second.second.join();
			iter->second.second = std::thread(&ChannelThd::proc, iter->second.first.get());
		}
		
		return true;
	}
	
	int epHandle = -1;
	
	for (auto &[handle, _] : epsInfo) {							//search for free endpoint
		bool unused = true;
		
		for (auto &[_, objs] : channelThds) {
			if (handle == objs.first->epHandle) {
				unused = false;
				break;
			}
		}
		
		if (unused) {
			epHandle = handle;
			break;
		}
	}
	
	if (epHandle == -1) {
		SPDLOG_ERROR("No endpoint available for channel {}.", idx);
	}
	else {
		const auto &iter = channelThds.emplace(idx,
			std::make_pair(std::make_unique<ChannelThd>(fd, idx, epHandle), std::thread()));
		iter.first->second.second = std::thread(&ChannelThd::proc, iter.first->second.first.get());
		
		SPDLOG_INFO("Channel {} thread added with endpoint handle '{}'.", idx, epHandle);
		
		return true;
	}
	
	return false;
}

//! Helper function to stop channel thread.
//! @param[in] idx Channel index.
//! @return Valid iterator pointing to target object if found and stopped, or \ref channelThds::end().
decltype(channelThds)::iterator channelThdStop(__u8 idx) {
	auto iter = channelThds.find(idx);
	
	if (iter == channelThds.end()) {
		SPDLOG_ERROR("Channel {} not found in thread list.", idx);
		return iter;
	}
	
	if (iter->second.first->isRunning()) {
		iter->second.first->stopProc();
	}
	iter->second.second.join();
	
	return iter;
}

//! Helper function to read data from host via EP0.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[out] io IO object for receiving data.
//! @return Bytes read if no error has occurred, or negative error code.
int readEp0(int fd, usb_raw_ep_io *io) {
	const int ioQt = ioctl(fd, USB_RAW_IOCTL_EP0_READ, io);
	
	if (ioQt < 0) [[unlikely]] {
		SPDLOG_ERROR("Error reading response for CTRL OUT request: {}", std::strerror(errno));
	}
	else [[likely]] {
		SPDLOG_TRACE("CTRL OUT request read {} byte(s).", ioQt);
	}
	
	return ioQt;
}

//! Helper function to send data to host via EP0.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] io Data to be sent.
//! @return True if no error has occurred.
bool writeEp0(int fd, usb_raw_ep_io *io) {
	const int ioQt = ioctl(fd, USB_RAW_IOCTL_EP0_WRITE, io);
	
	if (ioQt < 0) [[unlikely]] {
		SPDLOG_ERROR("Error writing response for CTRL IN request: {}", std::strerror(errno));
		return false;
	}
	SPDLOG_TRACE("CTRL IN request wrote {} byte(s).", ioQt);
	
	return true;
}

//! Handles control endpoint (EP0) standard IN (send data to host) request.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] req Request details.
//! @param[out] io Data to be sent.
//! @return True if no error has occurred.
bool ctrlStdInReqHandler(int fd, const usb_ctrlrequest *req, usb_raw_ep_io *io) {
	switch (req->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		SPDLOG_TRACE("CTRL STD IN GetDescriptor - wValue:{:#0x} wIndex:{:#0x} wLength:{}", req->wValue,
			req->wIndex, req->wLength);
			
		switch (req->wValue >> 8) {
		case USB_DT_DEVICE: {
			usb_device_descriptor desc {
				.bLength = USB_DT_DEVICE_SIZE,
				.bDescriptorType = USB_DT_DEVICE,
				.bcdUSB = __cpu_to_le16(0x0200),					//USB2.0 though full-speed only
				.bDeviceClass = 0,
				.bDeviceSubClass = 0,
				.bDeviceProtocol = 0,
				.bMaxPacketSize0 = MAX_PACKET_SIZE,
				.idVendor = __cpu_to_le16(USB_ID_VENDOR),
				.idProduct = __cpu_to_le16(USB_ID_PRODUCT),
				.bcdDevice = __cpu_to_le16(DEVICE_VERSION),
				.iManufacturer = static_cast<uint8_t>(StringId::MANUFACTURER),
				.iProduct = static_cast<uint8_t>(StringId::PRODUCT),
				.iSerialNumber = static_cast<uint8_t>(StringId::SERIAL),
				.bNumConfigurations = 1u
			};
			
			io->length = sizeof(desc);
			memcpy(io->data, &desc, sizeof(desc));
			if (!writeEp0(fd, io)) {
				return false;
			}
			
			break;
		}
		case USB_DT_DEVICE_QUALIFIER:
			//send request error as this device is full-speed only
			return false;
		case USB_DT_CONFIG: {
			size_t idx = 0u;
			
			auto fx = [&idx, io](void *data, size_t length) -> bool {
				if ((idx + length) > MAX_IO_DATA_LEN) {
					SPDLOG_ERROR("Config data to be sent not fit in current buffer.");
					return false;
				}
				
				memcpy(io->data + idx, data, length);
				idx += length;
				
				return true;
			};
			
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
			usb_config_descriptor descCfg {
				.bLength = USB_DT_CONFIG_SIZE,
				.bDescriptorType = USB_DT_CONFIG,
				.bNumInterfaces = 1,
				.bConfigurationValue = USB_SELECT_CONFIG,
				.iConfiguration = static_cast<uint8_t>(StringId::CONFIG),
				.bmAttributes = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
				.bMaxPower = USB_MAX_POWER
			};
			usb_interface_descriptor descIntf = {
				.bLength = USB_DT_INTERFACE_SIZE,
				.bDescriptorType = USB_DT_INTERFACE,
				.bInterfaceNumber = 0,
				.bAlternateSetting = USB_SELECT_INTERFACE,
				.bNumEndpoints = (__u8) epsInfo.size(),
				.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
				.bInterfaceSubClass = 1,
				.bInterfaceProtocol = 1,
				.iInterface = static_cast<uint8_t>(StringId::INTERFACE)
			};
			usb_endpoint_descriptor descEp = {
				.bLength = USB_DT_ENDPOINT_SIZE,
				.bDescriptorType = USB_DT_ENDPOINT,
				.wMaxPacketSize = MAX_PACKET_SIZE,
				.bInterval = 5
			};
			#pragma GCC diagnostic pop
			
			if (!fx(&descCfg, USB_DT_CONFIG_SIZE) || !fx(&descIntf, USB_DT_INTERFACE_SIZE)) {
				return false;
			}
			
			for (const auto& [unused, ep] : epsInfo) {
				if (ep.caps.dir_in) {
					descEp.bEndpointAddress = USB_DIR_IN | ep.addr;
				}
				else if (ep.caps.dir_out){
					descEp.bEndpointAddress = USB_DIR_OUT | ep.addr;
				}
				else {
					SPDLOG_CRITICAL("Unknown endpoint direction found when preparing config.");
					return false;
				}
				
				if (ep.caps.type_bulk) {
					descEp.bmAttributes = USB_ENDPOINT_XFER_BULK;
				}
				else if (ep.caps.type_control) {
					descEp.bmAttributes = USB_ENDPOINT_XFER_CONTROL;
				}
				else if (ep.caps.type_int) {
					descEp.bmAttributes = USB_ENDPOINT_XFER_INT;
				}
				else if (ep.caps.type_iso) {
					descEp.bmAttributes = USB_ENDPOINT_XFER_ISOC;
				}
				else {
					SPDLOG_ERROR("Unknown endpoint type found when preparing config.");
					return false;
				}
				
				if (!fx(&descEp, USB_DT_ENDPOINT_SIZE)) {
					return false;
				}
			}
			
			reinterpret_cast<usb_config_descriptor*>(io->data)->wTotalLength = __cpu_to_le16(idx);
			if (idx > req->wLength) {
				SPDLOG_WARN("Config descriptor data size {} > request wLength {}", idx, req->wLength);
				idx = req->wLength;
			}
			io->length = idx;
			if (!writeEp0(fd, io)) {
				return false;
			}
			
			break;
		}
		case USB_DT_STRING: {
			using namespace std::literals;
			
			std::u16string_view str;
			
			switch (static_cast<StringId>(req->wValue & 0xFF)) {
			case StringId::LANG_IDS:
				str = u"\x0908"sv;									//0x0809 - English (UK)
				break;
			case StringId::MANUFACTURER:
				str = u"x64 Factory"sv;
				break;
			case StringId::PRODUCT:
				str = u"x64 Product"sv;
				break;
			case StringId::SERIAL:
				str = u"ABCD-1234"sv;
				break;
			case StringId::CONFIG:
				str = u"Logic analyser"sv;
				break;
			case StringId::INTERFACE:
				str = u"Data transfer"sv;
				break;
			default:
				SPDLOG_WARN("Got unknown string descriptor index '{}'.", req->wValue & 0xFF);
				str = u"Unknown"sv;
				break;
			}
			
			const size_t strBytes = str.size() * sizeof(*str.data());	//multibyte char
			const size_t maxStrLen = std::min({size_t(req->wLength - offsetof(usb_string_descriptor, wData)),
				size_t(MAX_IO_DATA_LEN - offsetof(usb_string_descriptor, wData)),
				size_t(USB_MAX_STRING_LEN),
				strBytes});
			
			if (strBytes > maxStrLen) {
				SPDLOG_WARN("String descriptor data size {} truncated to {}", strBytes, maxStrLen);
			}
			
			auto desc = reinterpret_cast<usb_string_descriptor*>(io->data);
			desc->bLength = __u8(maxStrLen + offsetof(usb_string_descriptor, wData));
			desc->bDescriptorType = USB_DT_STRING;
			memcpy(desc->wData, str.data(), maxStrLen);
			io->length = desc->bLength;
			if (!writeEp0(fd, io)) {
				return false;
			}
			
			break;
		}
		default:
			SPDLOG_WARN("CTRL STD IN GetDescriptor ignored wValue:{:#0x} wIndex:{:#0x} wLength:{}",
				req->wValue, req->wIndex, req->wLength);
			return false;
		}
		break;
	case USB_REQ_GET_INTERFACE:
		io->data[0u] = USB_SELECT_INTERFACE;
		io->length = 1u;
		if (!writeEp0(fd, io)) {
			return false;
		}
		break;
	default:
		SPDLOG_WARN("CTRL STD IN ignored bRequest:{:#0x} wValue:{:#0x} wIndex:{:#0x} wLength:{}.",
			req->bRequest, req->wValue, req->wIndex, req->wLength);
		return false;
	}
	
	return true;
}

//! Handles control endpoint (EP0) standard OUT (receive data from host) request.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] req Request details.
//! @param[out] io IO object for receiving data.
//! @return True if no error has occurred.
bool ctrlStdOutReqHandler(int fd, const usb_ctrlrequest *req, usb_raw_ep_io *io) {
	switch (req->bRequest) {
	case USB_REQ_SET_CONFIGURATION: {
		const uint8_t config = req->wValue & 0xFF;
		
		SPDLOG_TRACE("CTRL STD OUT SetConfiguration - wValue:{:#0x} wIndex:{:#0x} wLength:{}", req->wValue,
			req->wIndex, req->wLength);
		
		if (config == USB_SELECT_CONFIG) {
			if (!enableEps(fd)) {
				return false;
			}
			
			if (ioctl(fd, USB_RAW_IOCTL_VBUS_DRAW, USB_MAX_POWER) < 0) {
				SPDLOG_ERROR("Error enabling USB VBUS power: {}", std::strerror(errno));
				return false;
			}
			
			if (ioctl(fd, USB_RAW_IOCTL_CONFIGURE, 0) < 0) {
				SPDLOG_ERROR("Error changing device state to configured: {}", std::strerror(errno));
				return false;
			}
			
			io->length = 0u;
			if (readEp0(fd, io) < 0) {								//need to read even though it's 0 length
				SPDLOG_ERROR("Error notifying host device state has configured: {}", std::strerror(errno));
				return false;
			}
		}
		else {
			SPDLOG_ERROR("Unknown configuration to be set '{}'.", config);
			return false;
		}
		
		break;
	}
	default:
		SPDLOG_WARN("CTRL STD OUT ignored device bRequest:{:#0x} wValue:{:#0x} wIndex:{:#0x} wLength:{}",
			req->bRequest, req->wValue, req->wIndex, req->wLength);
		return false;
	}
	
	return true;
}

//! Handles control endpoint (EP0) standard IN (send data to host) request.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] req Request details.
//! @param[out] io Data to be sent.
//! @return True if no error has occurred.
bool ctrlVndInReqHandler(int fd, const usb_ctrlrequest *req, usb_raw_ep_io *io) {
	switch (req->bRequest) {
	case USB_REQ_SEND_READING: {
		SPDLOG_TRACE("CTRL VND IN SendReading - wValue:{} wIndex:{} wLength:{}", req->wValue, req->wIndex,
			req->wLength);
		
		const __u8 chIdx = __le16_to_cpu(req->wIndex);
		auto iter = channelThds.find(chIdx);
		
		if (iter == channelThds.end()) [[unlikely]] {
			SPDLOG_ERROR("Channel {} not configured yet.", chIdx);
			return false;
		}
		
		io->length = __le16_to_cpu(req->wLength);
		if (io->length != sizeof(__le32)) [[unlikely]] {
			SPDLOG_ERROR("{} byte(s) expected data length doesn't match data size.", io->length);
			return false;
		}
		
		size_t maxSz = __le16_to_cpu(req->wValue);
		if (iter->second.first->getData(maxSz)) [[likely]] {
			const __le32 maxSzLe = __cpu_to_le32(maxSz);
			memcpy(io->data, &maxSzLe, sizeof(maxSzLe));
			
			if (!writeEp0(fd, io)) [[unlikely]] {
				SPDLOG_ERROR("Error notifying host device will send reading: {}", std::strerror(errno));
				return false;
			}
		}
		else {
			return false;
		}
		
		break;
	}
	case USB_REQ_GET_CONFIGURATION:
		SPDLOG_TRACE("CTRL VND IN GetConfiguration - wIndex:{} wLength:{}", req->wIndex, req->wLength);
		
		if (__le16_to_cpu(req->wLength) == sizeof(ch_config)) {
			const __u8 chIdx = __le16_to_cpu(req->wIndex);
			ch_config cfg;
			
			//'idx' field will be replaced with endpoint index instead
			if (getGeneratorConfig(chIdx, &cfg)) {
				auto iter = channelThds.find(chIdx);
				
				if (iter == channelThds.end()) {
					SPDLOG_ERROR("Channel {} not configured yet.", chIdx);
					return false;
				}
				else {
					const __u16 epHandle = iter->second.first->epHandle;
					cfg.idx = 0u;									//safe since endpoint 0 is default ctrl
					
					for (auto &[handle, info] : epsInfo) {
						if (handle == epHandle) {
							cfg.idx = info.addr;
							break;
						}
					}
					
					if (cfg.idx) {
						io->length = sizeof(ch_config);
						memcpy(io->data, &cfg, sizeof(ch_config));
						if (!writeEp0(fd, io)) {
							return false;
						}
					}
					else {
						SPDLOG_ERROR("Channel {} with handle {} not found in endpoint list.", chIdx,
							epHandle);
						return false;
					}
				}
			}
			else {
				return false;
			}
		}
		else {
			SPDLOG_ERROR("{} byte(s) data requested doesn't match channel config size.",
				__le16_to_cpu(req->wLength));
			return false;
		}
		
		break;
	default:
		SPDLOG_WARN("CTRL VND IN ignored vendor device bRequest:{:#0x} wValue:{:#0x} wIndex:{:#0x} " \
			"wLength:{}", req->bRequest, req->wValue, req->wIndex, req->wLength);
		return false;
	}
	
	return true;
}

//! Handles control endpoint (EP0) vendor OUT (receive data from host) request.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] req Request details.
//! @param[out] io IO object for receiving data.
//! @return True if no error has occurred.
bool ctrlVndOutReqHandler(int fd, const usb_ctrlrequest *req, usb_raw_ep_io *io) {
	switch (req->bRequest) {
	case USB_REQ_SET_CONFIGURATION: {
		SPDLOG_TRACE("CTRL VND OUT SetConfiguration - wLength:{}", req->wLength);
		
		io->length = __le16_to_cpu(req->wLength);
		const int ioQt = readEp0(fd, io);
		
		if (ioQt == sizeof(ch_config)) {
			auto cfg = reinterpret_cast<const ch_config*>(io->data);
			
			if (setGeneratorConfig(cfg)) [[likely]] {
				return channelThdAdd(fd, cfg->idx);
			}
			else {
				auto &&iter = channelThdStop(cfg->idx);			//remove channel with invalid config
				if (iter != channelThds.end()) {
					channelThds.erase(iter);
					SPDLOG_INFO("Channel {} thread removed.", cfg->idx);
				}
				
				return false;
			}
		}
		else {
			SPDLOG_ERROR("{} byte(s) data sent doesn't match channel config size.", ioQt);
			return false;
		}
		break;
	}
	default:
		SPDLOG_WARN("CTRL VND OUT ignored vendor device bRequest:{:#0x} wValue:{:#0x} wIndex:{:#0x} " \
			"wLength:{}", req->bRequest, req->wValue, req->wIndex, req->wLength);
		return false;
	}
	
	return true;
}

//! Handles control endpoint (EP0) event.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @param[in] req Event details.
//! @param[in,out] io IO object.
//! @return True if no error has occurred.
bool ctrlEvtHandler(int fd, const usb_ctrlrequest *req, usb_raw_ep_io *io) {
	switch (req->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		return (req->bRequestType & USB_DIR_IN) ? ctrlStdInReqHandler(fd, req, io) :
			ctrlStdOutReqHandler(fd, req, io);
	case USB_TYPE_VENDOR:
		return (req->bRequestType & USB_DIR_IN) ? ctrlVndInReqHandler(fd, req, io) :
			ctrlVndOutReqHandler(fd, req, io);
	default:
		SPDLOG_WARN("CTRL ignored bRequestType:{:#0x} bRequest:{:#0x} wValue:{:#0x} wIndex:{:#0x} " \
			"wLength:{}.", req->bRequestType, req->bRequest, req->wValue, req->wIndex, req->wLength);
	}
	
	return false;
}

//! Gets endpoints info supported by current UDC. So far it's called when device starts connection to UDC.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @return True if no error has occurred.
bool processEpsInfo(int fd) {
	usb_raw_eps_info info;
	memset(&info, 0, sizeof(info));

	const int epCount = ioctl(fd, USB_RAW_IOCTL_EPS_INFO, &info);

	if (epCount < 0) {
		SPDLOG_CRITICAL("Error getting UDC endpoint info: {}", std::strerror(errno));
		return false;
	}

	SPDLOG_DEBUG("UDC endpoint list:");

	epsInfo.clear();
	for (int idx = 0; idx < epCount; ++idx) {
		const usb_raw_ep_info &ep = info.eps[idx];
		const char *dir = NULL, *type = NULL;

		if (ep.caps.dir_in) {
			dir = "in";
		}
		else if (ep.caps.dir_out) {
			dir = "out";
		}
		else {
			SPDLOG_WARN("Endpoint {} has no direction set.", idx);
			continue;
		}

		if (ep.caps.type_control) {
			type = "control";
		}
		else if (ep.caps.type_iso) {
			type = "iso";
		}
		else if (ep.caps.type_bulk) {
			type = "bulk";
		}
		else if (ep.caps.type_int) {
			type = "int";
		}
		else {
			SPDLOG_WARN("Endpoint {} has no type set.", idx);
			continue;
		}

		SPDLOG_DEBUG("\t{}\tname: {}", idx, (char *) ep.name);
		SPDLOG_DEBUG("\t\taddr: {}", ep.addr);
		SPDLOG_DEBUG("\t\ttype: {}", type);
		SPDLOG_DEBUG("\t\tdirection: {}", dir);
		SPDLOG_DEBUG("\t\tmaxPacketSz: {}", ep.limits.maxpacket_limit);
		SPDLOG_DEBUG("\t\tmaxStrmCount: {}", ep.limits.max_streams);

		epsInfo.push_back(std::make_pair(-1, ep));
	}

	//assign address to selected endpoints matching expected usage (bulk-in)
	uint32_t freeIdx = 1u;
	for (auto iter = epsInfo.begin(); iter != epsInfo.end();) {
		usb_raw_ep_info &info = iter->second;
		
		if (info.caps.type_bulk && info.caps.dir_in) {
			if (info.addr == USB_RAW_EP_ADDR_ANY) {
				uint32_t testIdx = freeIdx;
				
				while (true) {
					bool unique = true;
					
					for (const auto& [ignored, info2] : epsInfo) {
						if (testIdx == info2.addr) {
							unique = false;
							break;
						}
					}
					
					if (unique) {
						break;
					}
					
					if (++testIdx >= 16u) {								//max valid endpoint address
						SPDLOG_CRITICAL("Exhausted endpoint address to assign.");
						return false;
					}
				}
				
				info.addr = testIdx;
				freeIdx = testIdx + 1u;
			}
			
			++iter;
		}
		else {
			iter = epsInfo.erase(iter);
		}
	}
	
	return true;
}

//! Enables UDC-specific endpoints.
//! @param[in] fd File descriptor for '/dev/raw-gadget'.
//! @return True if no error has occurred.
bool enableEps(int fd) {
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	usb_endpoint_descriptor desc {
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType =	USB_DT_ENDPOINT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = __cpu_to_le16(MAX_PACKET_SIZE),
		.bInterval = 5u
	};
	#pragma GCC diagnostic pop

	for (auto &[handle, info] : epsInfo) {
		desc.bEndpointAddress = USB_DIR_IN | info.addr;
		
		handle = ioctl(fd, USB_RAW_IOCTL_EP_ENABLE, &desc);
		if (handle < 0) {
			SPDLOG_CRITICAL("Error enabling endpoint addr:{}: {}", info.addr, std::strerror(errno));
			return false;
		}
		else {
			SPDLOG_TRACE("Enabled endpoint addr:{} handle:{}", info.addr, handle);
		}
	}

	SPDLOG_INFO("{} bulk-in endpoint(s) enabled.", epsInfo.size());

	return true;
}

bool startProc(std::string_view device, std::string_view driver, usb_device_speed speed) {
	std::unique_ptr<uint8_t[]>										//+buffer for event data
		evtRaw(new (std::nothrow) uint8_t[sizeof(usb_raw_event) + sizeof(usb_ctrlrequest)]),
		ioRaw(new (std::nothrow) uint8_t[sizeof(usb_raw_ep_io) + MAX_IO_DATA_LEN]);
	int result = 0;

	SPDLOG_TRACE("Proc start.");

	//not including NULL character
	if ((device.size() >= UDC_NAME_LENGTH_MAX) || (driver.size() >= UDC_NAME_LENGTH_MAX)) {
		SPDLOG_CRITICAL("Invalid 'device'/'driver' parameter length.");
		return false;
	}

	if (!evtRaw || !ioRaw) {
		SPDLOG_CRITICAL("Error allocating 'usb_raw_event'/'usb_raw_ep_io' object(s).");
		return false;
	}

	int fd = open("/dev/raw-gadget", O_RDWR);
	if (fd < 0) {
		SPDLOG_CRITICAL("Error opening '/dev/raw-gadget': {}", std::strerror(errno));
		return false;
	}

	{
		struct usb_raw_init arg;
		std::copy_n(device.data(), device.size() + 1u, arg.device_name);	//with NULL character
		std::copy_n(driver.data(), driver.size() + 1u, arg.driver_name);
		arg.speed = speed;

		result = ioctl(fd, USB_RAW_IOCTL_INIT, &arg);
		if (result < 0) {
			SPDLOG_CRITICAL("ioctl(USB_RAW_IOCTL_INIT): {}", std::strerror(errno));
			goto err_1;
		}
	}

	result = ioctl(fd, USB_RAW_IOCTL_RUN, 0);
	if (result < 0) {
		SPDLOG_CRITICAL("ioctl(USB_RAW_IOCTL_RUN): {}", std::strerror(errno));
		goto err_1;
	}

	while (run) {
		auto evt = reinterpret_cast<usb_raw_event*>(evtRaw.get());
		evt->type = USB_RAW_EVENT_INVALID;
		evt->length = sizeof(usb_ctrlrequest);

		result = ioctl(fd, USB_RAW_IOCTL_EVENT_FETCH, evt);
		if (result < 0) {
			SPDLOG_CRITICAL("ioctl(USB_RAW_IOCTL_EVENT_FETCH): {}", std::strerror(errno));
			run = false;
		}

		switch (evt->type) {
		case USB_RAW_EVENT_CONTROL: {
			if (ctrlEvtHandler(fd, reinterpret_cast<const usb_ctrlrequest*>(evt->data),
				reinterpret_cast<usb_raw_ep_io*>(ioRaw.get()))) {}
			else if (ioctl(fd, USB_RAW_IOCTL_EP0_STALL, 0) < 0) [[unlikely]] {
				SPDLOG_CRITICAL("Error stalling on EP0 as request error: {}", std::strerror(errno));
				run = false;
			}
			
			break;
		}
		case USB_RAW_EVENT_CONNECT:
			if (!processEpsInfo(fd)) {
				run = false;
			}
			break;
		default:
			SPDLOG_WARN("Got unhandled fetched event '{}' with {} byte(s).", evt->type, evt->length);
			break;
		}
	}

	for (auto &[idx, _] : channelThds) {
		channelThdStop(idx);
	}
	channelThds.clear();
	
	SPDLOG_TRACE("Proc end.");

err_1:
	close(fd);

	return result;
}

void stopProc() {
	SPDLOG_TRACE("Stopping proc...");
	run = false;
}
