#define MODULE_NAME "rpi_host_low"

#define pr_fmt(fmt) MODULE_NAME ": " fmt "\n"

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/kref.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/version.h>

//! USB IN vendor request for notifying device to send channel readings.
#define USB_REQ_SEND_READING	50

//! Get a minor range for your devices from the usb maintainer.
#define USB_SKEL_MINOR_BASE		192
#define USB_EP_BUF_LEN 			1024u								//!< USB transfer buffer size, in bytes.
#define USB_ID_VENDOR			0x0627u								//!< USB vendor ID
#define USB_ID_PRODUCT			0x0001u								//!< USB product ID

#define CDEV_DEVICE_CLASS_NAME	"rpi_cdev_class"
#define CDEV_KOBJ_NAME_FMT		"rpi_cdev_kobj_%u_%u"				//!< dev name as shown in /proc/devices
#define CDEV_REGION_NAME		"rpi_cdev_region"
#define CDEV_ROOT_DEVICE_NAME	"rpi_cdev_root"
#define CDEV_DEVICE_BASE_MINOR	192u
#define CDEV_DEVICE_NAME_FMT	"%u_%u"								//!< USB interface minor + channel index
#define CDEV_DEVICE_MAX_COUNT	15u

//! Indices for sysfs parameter attributes.
#define SYSFS_ATTR_CH_CFG_MIN	0u
#define SYSFS_ATTR_CH_CFG_MAX	14u
#define SYSFS_ATTR_CH_COUNT		15u

//! Used in \ref ch_data structure.
#define SAMPLE_BITS				4u
#define SAMPLE_PER_READING		4u
#define TAG_BITS				28u

//table of devices that work with this driver
static const struct usb_device_id skel_table[] = {
	{ USB_DEVICE(USB_ID_VENDOR, USB_ID_PRODUCT) },
	{ }					//Terminating entry
};
MODULE_DEVICE_TABLE(usb, skel_table);

//! Format of data sent to (or received from) USB control endpoint to set (or get) channel config.
struct ch_config {
	//! Channel index or endpoint address, depending on direction.
	__u8 idx;
	__u8 pinbase;													//!< Pin base index.
	__u8 pincount;													//!< Pin count.
	__le32 rate;													//!< Sampling rate, in Hz.
} __attribute__ ((packed));

//! Format of data sent to client as single logic analyser reading.
struct ch_data {
	//! \ref data valid entry position. MSB-&gt;LSB bits = low-&gt;high index. Set bit = valid sample.
	__le32 valid:SAMPLE_BITS;
	//! Ever increasing tag for this reading. May overflow to 0.
	__le32 tag:TAG_BITS;
	//! Reading samples. Each sample LSB-&gt;MSB = low-&gt;high pin index.
	__u8 data[SAMPLE_PER_READING];
} __attribute__ ((packed));
static_assert(!(USB_EP_BUF_LEN % sizeof(struct ch_data)),
	"Endpoint buffer length should be multiples of expected channel readings data.");

//! Data used by each USB endpoint. Also used as URB context.
struct ep_data {
	//! Transfer buffer allocated using \ref usb_alloc_coherent() with size \ref USB_EP_BUF_LEN bytes.
	u8 *buf;
	u8 ep_num;														//!< Endpoint number (without direction).
	struct urb *urb;												//!< URB object for this endpoint.
	//! Setup packet used for read (USB IN) operation only. The request done via this packet is fixed (not
	//! to be reused for different requests):
	//!		[0]: default control -&gt; vendor GET_CONFIGURATION
	//!		[1..]: bulk-in -&gt; vendor SEND_READING
	//! \ref ep_data::setup::wIndex refers to channel index associated with this endpoint. Set to
	//! \ref UINT8_MAX if channel is invalid.
	struct usb_ctrlrequest setup;
	
	//! How many bytes currently in \ref ep_data::buf.
	size_t buf_count;
	//! How many bytes has been copied from \ref ep_data::buf.
	size_t buf_offset;
	
	//! Last operation status. Can only be read safely when there's no ongoing operation.
	int last_err;
	//! True if there's ongoing operation. Protected by \ref usb_skel::op_lock.
	bool ongoing;
	struct mutex op_mutex;											//!< Concurrent operation mutex.
	struct lock_class_key op_mutex_key;								//!< Used by \ref op_mutex.
	
	//! This object index within \ref usb_skel::ep_data_head array, starting from 0. Used to get array base
	//! address and then containing \ref usb_skel object address.
	u8 idx;
};

//! Header to contain pointer to \ref usb_skel just to avoid putting it in each \ref ep_data item.
struct ep_data_head {
	struct usb_skel *dev;											//!< Pointer to \ref usb_skel object.
	struct ep_data list[];											//!< List of \ref ep_data objects.
};

//! sysfs-related parameter attributes.
struct param_attr {
	#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lock_class_key key;										//!< Lockdep object.
	#endif
	struct attribute attr;											//!< Attribute object.
	char name[8u];													//!< Attribute name.
};

//! Structure to hold all of our device specific stuff.
struct usb_skel {
	//Is character device open? Used to prevent multiple accesses to device file.
	atomic_t already_open[CDEV_DEVICE_MAX_COUNT];
	struct cdev *cdev_objs[CDEV_DEVICE_MAX_COUNT];
	unsigned int cdev_major_id;
	
	//! sysfs parameter attributes.
	//! [SYSFS_ATTR_CH_CFG_MIN-SYSFS_ATTR_CH_CFG_MAX]: Channel configuration.
	//! [SYSFS_ATTR_CH_COUNT]: Channel count.
	struct param_attr sysfs_param_attrs[SYSFS_ATTR_CH_COUNT + 1u];
	u8 sysfs_ch_cfg_pinbase[SYSFS_ATTR_CH_CFG_MAX + 1u];			//!< sysfs param: channel pin base index.
	u8 sysfs_ch_cfg_pincount[SYSFS_ATTR_CH_CFG_MAX + 1u];			//!< sysfs param: channel pin count.
	u32 sysfs_ch_cfg_rate[SYSFS_ATTR_CH_CFG_MAX + 1u];				//!< sysfs param: channel sampling rate.
	u8 sysfs_ch_count;												//!< sysfs param: channel count.
	
	struct usb_interface *interface;								//!< the interface for this device
	struct kobject kobj;											//!< Also used in sysfs setup.
	struct usb_device *udev;										//!< the usb device for this device

	//! \ref usb_skel::ep_data_head::list array size.
	u8 ep_count;
	//! Array of endpoints' data. It's expected that [0]=default control and the rest is bulk-in(s).
	struct ep_data_head *ep_data_head;
	
	//! True if device is to be disconnected from system.
	bool disconnected;
	struct rw_semaphore disconnected_sem;							//!< Protects \ref disconnected flag.
	struct lock_class_key disconnected_sem_key;						//!< Used by \ref disconnected_sem.
	spinlock_t op_lock;												//!< Operation status lock.
	wait_queue_head_t wait;											//!< Wait object for ongoing operation.
};

//forward declarations
static int set_dev_to_file(int minor, struct file *file);
static int unset_dev_from_file(struct file *file);

//! Helper function to setup \ref ep_data members.
static int ep_data_setup(struct usb_device *udev, struct ep_data *ep_data_base, u8 elem_idx, u8 ep_num) {
	struct ep_data *ep_data = ep_data_base + elem_idx;
	
	ep_data->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep_data->urb) {
		return -ENOMEM;
	}
	
	ep_data->buf = usb_alloc_coherent(udev, USB_EP_BUF_LEN, GFP_KERNEL, &ep_data->urb->transfer_dma);
	if (!ep_data->buf) {
		return -ENOMEM;
	}
	
	if (elem_idx) {
		ep_data->setup.bRequestType = USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
		ep_data->setup.bRequest = USB_REQ_SEND_READING;
		ep_data->setup.wLength = sizeof(__s32);
	}
	else {
		ep_data->setup.bRequestType = USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
		ep_data->setup.bRequest = USB_REQ_GET_CONFIGURATION;
		ep_data->setup.wLength = sizeof(struct ch_config);
	}
	ep_data->setup.wValue = 0u;
	ep_data->setup.wIndex = U8_MAX;
	
	ep_data->ep_num = ep_num;
	ep_data->idx = elem_idx;
	
	lockdep_register_key(&ep_data->op_mutex_key);
	__mutex_init(&ep_data->op_mutex, "op_mutex", &ep_data->op_mutex_key);
	
	return 0;
}

//! Helper function to free \ref ep_data members.
static void ep_data_cleanup(struct usb_device *udev, struct ep_data *ep_data) {
	if (ep_data->urb) {
		if (ep_data->buf) {
			usb_free_coherent(udev, USB_EP_BUF_LEN, ep_data->buf, ep_data->urb->transfer_dma);
			ep_data->buf = NULL;
		}
		
		usb_free_urb(ep_data->urb);
		ep_data->urb = NULL;
	}
	
	lockdep_unregister_key(&ep_data->op_mutex_key);
}

//! Helper function to get \ref usb_skel object.
//! @param[in] ep_data Target \ref ep_data object.
//! @return Pointer to store pointer to \ref usb_skel object.
static inline struct usb_skel* ep_data_get_dev(struct ep_data *ep_data) {
	return container_of((void*)(ep_data - ep_data->idx), struct ep_data_head, list)->dev;
}

//USB-related functions
//**************************************************************************************
//! IRQ handler for read URB completion. Sets operation status (error) and actual data length received, and
//! resets ongoing read flag.
static void skel_read_callback(struct urb *urb) {
	struct ep_data *ep_data = urb->context;
	struct usb_skel *dev = ep_data_get_dev(ep_data);
	unsigned long flags;
	
	spin_lock_irqsave(&dev->op_lock, flags);
	if (unlikely(urb->status < 0)) {
		ep_data->last_err = urb->status;
	}
	else {
		ep_data->buf_count = urb->actual_length;
	}
	ep_data->ongoing = false;
	spin_unlock_irqrestore(&dev->op_lock, flags);
	
	wake_up_interruptible(&dev->wait);
}

//! Helper function to prepare URB for read operation.
static int skel_do_read_io(struct usb_skel *dev, struct ep_data *ep_data, struct usb_ctrlrequest *setup,
size_t count) {
	int result;
	
	if (ep_data->idx) {
		usb_fill_bulk_urb(ep_data->urb, dev->udev, usb_rcvbulkpipe(dev->udev, ep_data->ep_num), ep_data->buf,
			min_t(size_t, USB_EP_BUF_LEN, count), skel_read_callback, ep_data);
	}
	else {
		size_t read_sz = min_t(size_t, USB_EP_BUF_LEN, count);
		
		setup->wLength = cpu_to_le16(read_sz);
		usb_fill_control_urb(ep_data->urb, dev->udev, usb_rcvctrlpipe(dev->udev, ep_data->ep_num),
			(unsigned char*) setup, ep_data->buf, read_sz, skel_read_callback, ep_data);
	}
	ep_data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	//submit bulk in urb, which means no data to deliver
	ep_data->buf_count = 0u;
	ep_data->buf_offset = 0u;
	
	ep_data->ongoing = true;
	result = usb_submit_urb(ep_data->urb, GFP_KERNEL);
	if (unlikely(result < 0)) {
		dev_err(&dev->interface->dev, "Failed submitting read urb: %d", result);
		ep_data->ongoing = false;
	}
	
	return result;
}

//! Read operation to USB endpoint. No concurrent RW is allowed. May wait until operation finishes (always
//! blocking).
//! @param[in] dev User data object.
//! @param[in] ep_data \ref ep_data object in \ref usb_skel::ep_data array.
//! @param[in] setup Setup packet. Must not be NULL for control transfer. Must be valid and unmodified
//!					 throughout operation.
//! @param[out] buffer Data buffer.
//! @param[in] buffer_in_userspace True if \b buffer is in user space (to use copy_to_user()).
//! @param[in] count User buffer size and read data size being requested, in bytes.
//! @return How many bytes actually being read, or -ve error value.
static ssize_t own_usb_read(struct usb_skel *dev, struct ep_data *ep_data, struct usb_ctrlrequest *setup,
char *buffer, bool buffer_in_userspace, size_t count) {
	size_t buffer_offset = 0u;
	int result;
	bool fresh_io = false, ongoing;
	
	if (unlikely(!buffer)) {
		dev_err(&dev->interface->dev, "Buffer must be valid for IN transfer.");
		return -EINVAL;
	}
	if (unlikely(!count)) {
		dev_err(&dev->interface->dev, "IN transfer must request some bytes.");
		return -EINVAL;
	}
	if (unlikely(!ep_data->idx && !setup)) {
		dev_err(&dev->interface->dev, "Control transfer require valid setup packet.");
		return -EINVAL;
	}
	
	result = mutex_lock_interruptible(&ep_data->op_mutex);			//no concurrent operation
	if (unlikely(result < 0)) {
		return result;
	}
	
	result = down_read_interruptible(&dev->disconnected_sem);
	if (unlikely(result < 0)) {
		return result;
	}
	if (unlikely(dev->disconnected)) {								//device is being disconnected
		result = -ENODEV;
		goto err_1;
	}
	
retry:
	//check whether there's ongoing operation since read can be done multiple times in single call
	//**********************************************************************************
	spin_lock_irq(&dev->op_lock);
	ongoing = ep_data->ongoing;
	spin_unlock_irq(&dev->op_lock);
	
	if (ongoing) {
		//IO may take forever hence wait in an interruptible state
		result = wait_event_interruptible(dev->wait, (!ep_data->ongoing));
		if (unlikely(result < 0)) {
			//to allow setting return value to what has been received until now
			result = 0;
			goto err_1;
		}
	}
	//**********************************************************************************
	
	result = ep_data->last_err;										//errors must be reported
	if (unlikely(result < 0)) {
		ep_data->last_err = 0;										//any error is reported once
		result = (result == -EPIPE) ? result : -EIO;				//to preserve notifications about reset
		goto err_1;													//report it
	}
	
	//if buffer has uncopied data we may satisfy the read else we need to start IO
	if (ep_data->buf_count - ep_data->buf_offset) {
		size_t chunk = min(ep_data->buf_count - ep_data->buf_offset, count);
		
		if (buffer_in_userspace) {
			result = copy_to_user(buffer + buffer_offset, ep_data->buf + ep_data->buf_offset, chunk);
			if (unlikely(result < 0)) {
				goto err_1;
			}
		}
		else {
			memcpy(buffer + buffer_offset, ep_data->buf + ep_data->buf_offset, chunk);
		}
		buffer_offset += chunk;
		ep_data->buf_offset += chunk;
		
		if (buffer_offset < count) {								//data not enough, may need new IO
			//there's still data left from device; received data size is due to USB buffer limit.
			//for bulk-in, since device has promised to send 'count' byte(s), regardless from ongoing or
			//new operation, this should be safe (not waiting forever for device to send more data than
			//told).
			if (!fresh_io || (ep_data->buf_count == USB_EP_BUF_LEN)) {
				result = skel_do_read_io(dev, ep_data, setup, count - buffer_offset);
				if (likely(!result)) {
					fresh_io = true;								//current request is fresh read IO
					goto retry;
				}
			}
		}
	}
	else {															//no data in the buffer
		result = skel_do_read_io(dev, ep_data, setup, count);
		if (likely(!result)) {
			fresh_io = true;										//current request is fresh read IO
			goto retry;
		}
	}
	
err_1:
	up_read(&dev->disconnected_sem);
	mutex_unlock(&ep_data->op_mutex);
	
	if (likely(!result)) {
		result = buffer_offset;
	}
	
	return result;
}

static void skel_write_callback(struct urb *urb) {
	struct ep_data *ep_data = urb->context;
	struct usb_skel *dev = ep_data_get_dev(ep_data);
	
	if (urb->status) {
		//sync/async unlink faults aren't errors
		if (!(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN)) {
			dev_err(&dev->interface->dev, "Nonzero write status received: %d", urb->status);
		}
		ep_data->last_err = urb->status;
	}
	ep_data->ongoing = false;
	
	wake_up_interruptible(&dev->wait);
}

//! Write operation to USB control endpoint only. No concurrent RW is allowed. May wait until operation
//! finishes (always blocking).
//! @param[in] dev User data object.
//! @param[in] ep_data \ref ep_data object in \ref usb_skel::ep_data array. Only control endpoint is accepted
//!					   (index 0).
//! @param[in] setup Setup packet. Must be valid and unmodified throughout operation.
//! @param[out] buffer Data buffer.
//! @param[in] buffer_in_userspace True if \b buffer is in user space (to use copy_from_user()).
//! @param[in] count User buffer size and write data size being requested, in bytes.
//! @return Data size being written, in bytes, or error code if any.
static ssize_t own_usb_write(struct usb_skel *dev, struct ep_data *ep_data,
struct usb_ctrlrequest *setup, const char *buffer, bool buffer_in_userspace, size_t count) {
	const size_t write_sz = min_t(size_t, USB_EP_BUF_LEN, count);
	int result;
	
	if (ep_data->idx) {
		dev_err(&dev->interface->dev, "Invalid endpoint index '%u' for writing.", ep_data->idx);
		return -EINVAL;
	}
	
	if (write_sz && !buffer) {
		dev_err(&dev->interface->dev, "Buffer must be valid if data transfer requested.");
		return -EINVAL;
	}
	
	result = mutex_lock_interruptible(&ep_data->op_mutex);			//no concurrent operation
	if (result < 0) {
		return result;
	}
	
	result = down_read_interruptible(&dev->disconnected_sem);
	if (result < 0) {
		return result;
	}
	if (dev->disconnected) {										//device is being disconnected
		result = -ENODEV;
		goto err_1;
	}
	
	if (buffer_in_userspace) {
		if (copy_from_user(ep_data->buf, buffer, write_sz)) {
			result = -EFAULT;
			goto err_1;
		}
	}
	else {
		memcpy(ep_data->buf, buffer, write_sz);
	}
	
	setup->wLength = __cpu_to_le16(write_sz);						//setting up URB
	usb_fill_control_urb(ep_data->urb, dev->udev, usb_sndctrlpipe(dev->udev, ep_data->ep_num),
		(unsigned char*) setup, ep_data->buf, write_sz, skel_write_callback, ep_data);
	ep_data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	ep_data->ongoing = true;
	result = usb_submit_urb(ep_data->urb, GFP_KERNEL);
	if (result < 0) {
		dev_err(&dev->interface->dev, "Failed submitting write urb: %d", result);
		ep_data->ongoing = false;
	}
	else {															//wait for operation to finish
		result = wait_event_interruptible(dev->wait, (!ep_data->ongoing));
		if (likely(!result)) {
			if (unlikely(ep_data->last_err < 0)) {
				result = (ep_data->last_err == -EPIPE) ? ep_data->last_err : -EIO;
				ep_data->last_err = 0;								//any error is reported once
			}
			else {
				result = write_sz;
			}
		}
	}
	
err_1:
	up_read(&dev->disconnected_sem);
	mutex_unlock(&ep_data->op_mutex);

	return result;
}

//! Handles flush operation on character device file by returning+resets USB operation error.
//! @param[in] dev User data object.
//! @param[in] ep_data \ref ep_data object in \ref usb_skel::ep_data array.
//! @return 0 if no error has occurred.
static int own_usb_flush(struct usb_skel *dev, struct ep_data *ep_data) {
	int result;
	
	mutex_lock(&ep_data->op_mutex);									//wait for operation to stop
	usb_kill_urb(ep_data->urb);										//probably not needed
	
	//read out errors, leave subsequent opens a clean slate
	spin_lock_irq(&dev->op_lock);
	result = ep_data->last_err ? (ep_data->last_err == -EPIPE ? -EPIPE : -EIO) : 0;
	ep_data->last_err = 0;
	spin_unlock_irq(&dev->op_lock);
	
	mutex_unlock(&ep_data->op_mutex);
	
	return result;
}
//**************************************************************************************

//character device (cdev) file operations for user<->device data transfer
//**************************************************************************************
enum {
	CDEV_NOT_USED = 0,
	CDEV_EXCLUSIVE_OPEN = 1
};

static struct class *cdev_cls = NULL;
static struct device *cdev_dev_root = NULL;

//! Helper function to extract USB interface minor number and channel index from cdev filename. Also validates
//! extracted channel index value.
//! @param[in] name cdev filename, expected to be in \ref CDEV_DEVICE_NAME_FMT format.
//! @param[out] intf_minor Extracted USB interface minor number.
//! @param[out] ch_idx Extracted channel index.
//! @return 0 if filename is formatted correctly and channel index within range.
static int get_intf_minor(const unsigned char *name, unsigned int *intf_minor, unsigned int *ch_idx) {
	if (sscanf((const char*) name, CDEV_DEVICE_NAME_FMT, intf_minor, ch_idx) != 2) {
		pr_err("Unknown cdev filename '%s' formatting.", name);
		return -EINVAL;
	}
	
	return 0;
}

//! Called when cdev file is to be flushed.
int own_cdev_flush(struct file *file, fl_owner_t id) {
	struct ep_data *ep_data = file->private_data;
	struct usb_skel *dev = ep_data_get_dev(ep_data);
	int result;
	
	(void) id;
	
	result = own_usb_flush(dev, ep_data);
	
	return result;
}

//! Called when a process tries to open cdev file, like "sudo cat /dev/chardev"
static int own_cdev_open(struct inode *inode, struct file *file) {
	const unsigned int file_minor = MINOR(inode->i_rdev);
	unsigned int ch_idx, intf_minor, result;
	struct usb_skel *dev;
	
	//ensure target file minor number matches what this module expects
	if ((file_minor < CDEV_DEVICE_BASE_MINOR) ||
	(file_minor >= (CDEV_DEVICE_BASE_MINOR + CDEV_DEVICE_MAX_COUNT))) {
		pr_err("Got sent unknown cdev file with minor number '%u' for opening.", file_minor);
		return -ENXIO;
	}
	
	result = get_intf_minor(file->f_path.dentry->d_name.name, &intf_minor, &ch_idx);
	if (result) {
		return result;
	}
	
	result = set_dev_to_file(intf_minor, file);
	if (result) {
		return result;
	}
	dev = file->private_data;
	
	//search for endpoint corresponding to this channel
	for (u8 ep_idx = 1u; ep_idx < dev->ep_count; ++ep_idx) {		//skip default control
		if (dev->ep_data_head->list[ep_idx].setup.wIndex == ch_idx) {
			file->private_data = dev->ep_data_head->list + ep_idx;
			break;
		}
	}
	if (file->private_data == dev) {
		dev_err(&dev->udev->dev, "cdev channel '%u' doesn't have mapping to any endpoint.", ch_idx);
		result = -ENXIO;
		goto err_1;
	}
	
	//only single process is allowed at a time
	if (atomic_cmpxchg(dev->already_open + ch_idx, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN)) {
		result = -EBUSY;
		goto err_1;
	}
	
	if (!try_module_get(THIS_MODULE)) {
		result = -EPIPE;
		goto err_2;
	}
	
	return 0;
	
err_2:
	atomic_set(dev->already_open + ch_idx, CDEV_NOT_USED);
err_1:
	unset_dev_from_file(file);

	return result;
}

//! Called when a process closes the device file.
static int own_cdev_release(struct inode *inode, struct file *file) {
	const unsigned int file_minor = MINOR(inode->i_rdev);
	struct ep_data *ep_data = file->private_data;
	struct usb_skel *dev = ep_data_get_dev(ep_data);
	
	atomic_set(dev->already_open + (file_minor - CDEV_DEVICE_BASE_MINOR), CDEV_NOT_USED);
	
	file->private_data = dev;
	unset_dev_from_file(file);
	
	module_put(THIS_MODULE);
	
	return 0;
}

//! Called when a process, which already opened the dev file, attempts to read from it.
//! @param[in] file cdev file object.
//! @param[out] buffer Buffer to be filled with requested data.
//! @param[in] length Requested data size, along with \b buffer size, in bytes. Must fit and in multiples of
//!					  sizeof(\ref ch_data).
//! @param[in,out] offset Not used.
//! @return Data byte count in \b buffer, or &lt; 0 as error codes.
static ssize_t own_cdev_read(struct file *file, char __user *buffer, size_t length, loff_t *offset) {
	struct ep_data *ep_data = file->private_data;
	struct usb_skel *dev = ep_data_get_dev(ep_data);
	__le32 length_dev = length - (length % sizeof(struct ch_data));
	ssize_t result;
	
	(void) offset;
	
	if (!length_dev) {
		dev_err(&dev->interface->dev, "Length too short to fit channel data.");
		return -EINVAL;
	}
	
	ep_data->setup.wValue = __cpu_to_le16(length_dev);				//tell device to send data
	result = own_usb_read(dev, dev->ep_data_head->list, &ep_data->setup, (char*) &length_dev, false,
		sizeof(length_dev));
	if (unlikely(result < 0)) {
		return result;
	}
	
	length_dev = __le32_to_cpu(length_dev);
	
	//get data from channel bulk-in endpoint
	result = own_usb_read(dev, ep_data, NULL, buffer, true, length_dev);
	if (unlikely(result < 0)) {
		if (likely(result == -EPIPE)) {								//clear endpoint halt
			struct usb_ctrlrequest setup = {
				.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT,
				.bRequest = USB_REQ_CLEAR_FEATURE,
				.wValue = cpu_to_le16(USB_ENDPOINT_HALT),
				.wIndex = cpu_to_le16(ep_data->ep_num),
				.wLength = 0
			};
			
			dev_info(&dev->interface->dev, "Clearing endpoint %u halt status.", ep_data->ep_num);
			if (own_usb_write(dev, dev->ep_data_head->list, &setup, NULL, false, 0u) < 0) {
				dev_err(&dev->interface->dev, "Error clearing endpoint %u halt status.", ep_data->ep_num);
			}
		}
		
		return result;
	}
	
	dev_info(&dev->udev->dev, "Read byte requested:%zu device:%u got:%zd.", length, length_dev, result);
	
	return result;
}

static struct file_operations cdev_fops = {
	.owner = THIS_MODULE,
	.read = own_cdev_read,
	.open = own_cdev_open,
	.flush = own_cdev_flush,
	.release = own_cdev_release
};

//! Creates device file for char device used for USB channel IO.
//! @param[in] dev User data object.
//! @param[in] index Channel index, starting from 0.
//! @return 0 if index is valid and file is created successfully.
static int own_cdev_create_dev(struct usb_skel *dev, u8 index) {
	const dev_t cdev_id = MKDEV(dev->cdev_major_id, CDEV_DEVICE_BASE_MINOR + index);
	struct cdev *cdev_obj;
	struct device *cdev_dev;
	int result;

	if (index >= CDEV_DEVICE_MAX_COUNT) {
		dev_err(&dev->interface->dev, "cdev channel index '%u' out-of-range.", index);
		return -EBADF;
	}
	
	if (dev->cdev_objs[index]) {
		dev_err(&dev->interface->dev, "cdev for channel '%u' already exists.", index);
		return -EEXIST;
	}
	
	cdev_obj = cdev_alloc();
	
	if (cdev_obj) {
		cdev_obj->ops = &cdev_fops;
		cdev_obj->owner = THIS_MODULE;
		kobject_set_name(&cdev_obj->kobj, CDEV_KOBJ_NAME_FMT, dev->interface->minor, index);
		
		result = cdev_add(cdev_obj, cdev_id, 1u);
		if (result < 0) {
			dev_err(&dev->interface->dev, "Error adding cdev for channel '%u': %d", index, result);
			goto err;
		}
	}
	else {
		pr_err("Error allocating cdev[%u].", index);
		return -ENOMEM;
	}
	
	cdev_dev = device_create(cdev_cls, cdev_dev_root, cdev_id, NULL, CDEV_DEVICE_NAME_FMT,
		dev->interface->minor, index);
	
	if (IS_ERR(cdev_dev)) {
		result = (int) PTR_ERR(cdev_dev);
		dev_err(&dev->interface->dev, "Error creating cdev for channel '%u': %d", index, result);
		goto err;
	}
	
	dev->cdev_objs[index] = cdev_obj;
	
	dev_info(&dev->interface->dev, "cdev for channel '%u' created.", index);
	
	return 0;
	
err:
	cdev_del(cdev_obj);
	
	return result;
}

//! Removes device file for char device used for USB channel IO.
//! @param[in] dev User data object.
//! @param[in]] index Channel index, starting from 0.
//! @return 0 if index is valid.
static int own_cdev_delete_dev(struct usb_skel *dev, u8 index) {
	if (index >= CDEV_DEVICE_MAX_COUNT) {
		dev_err(&dev->interface->dev, "cdev channel index '%u' out-of-range.", index);
		return -EBADF;
	}
	
	if (dev->cdev_objs[index]) {
		device_destroy(cdev_cls, MKDEV(dev->cdev_major_id, CDEV_DEVICE_BASE_MINOR + index));
		cdev_del(dev->cdev_objs[index]);
		dev->cdev_objs[index] = NULL;
	}
	
	return 0;
}

//! Handles char device local setup (used by specific device) during USB device probe/creation.
//! @param[in] dev User data object.
static int own_cdev_setup_local(struct usb_skel *dev) {
	dev_t cdev_base_id;
	int result = alloc_chrdev_region(&cdev_base_id, CDEV_DEVICE_BASE_MINOR, CDEV_DEVICE_MAX_COUNT,
		CDEV_REGION_NAME);

	if (result < 0) {
		dev_err(&dev->interface->dev, "Registering cdev failed with %d", result);
		return result;
	}
	
	for (u8 index = 0; index < CDEV_DEVICE_MAX_COUNT; ++index) {
		atomic_set(dev->already_open + index, CDEV_NOT_USED);
	}
	dev->cdev_major_id = MAJOR(cdev_base_id);
	
	dev_info(&dev->interface->dev, "cdev got dynamic major ID '%u'.", dev->cdev_major_id);
	
	return 0;
}

//! Handles character device global setup (used by all devices) during module init.
static int own_cdev_setup_global(void) {
	int result;
	
	cdev_dev_root = root_device_register(CDEV_ROOT_DEVICE_NAME);
	if (IS_ERR(cdev_dev_root)) {
		result = (int) PTR_ERR(cdev_dev_root);
		pr_err("Error creating root device: %d", result);
		goto err_1;
	}
	
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	cdev_cls = class_create(CDEV_DEVICE_CLASS_NAME);
	#else
	cdev_cls = class_create(THIS_MODULE, CDEV_DEVICE_CLASS_NAME);
	#endif
	if (IS_ERR(cdev_cls)) {
		result = (int) PTR_ERR(cdev_cls);
		pr_err("Error creating class for cdev creation: %d", result);
		goto err_2;
	}
	
	return 0;

err_2:
	root_device_unregister(cdev_dev_root);
err_1:
	return result;
}

//! Removes char devices for specific USB device from system.
//! @param[in] dev User data object.
static void own_cdev_cleanup_local(struct usb_skel *dev) {
	for (u8 index = 0; index < CDEV_DEVICE_MAX_COUNT; ++index) {
		own_cdev_delete_dev(dev, index);
	}
	unregister_chrdev_region(MKDEV(dev->cdev_major_id, CDEV_DEVICE_BASE_MINOR), CDEV_DEVICE_MAX_COUNT);
}

static void own_cdev_cleanup_global(void) {
	class_destroy(cdev_cls);
	root_device_unregister(cdev_dev_root);
}
//**************************************************************************************

//sysfs entries for device parameters
//**************************************************************************************
static int own_sysfs_param_setup(struct usb_skel *dev, u8 index);
static void own_sysfs_param_cleanup(struct usb_skel *dev, u8 index);

//! Helper function to get channel config from device and set corresponding variables based on data received.
//! @param[in] dev User data object.
//! @param[in] index Channel index, starting from 0.
//! @return 0 if such channel exists on device, and channel endpoint is known on host side.
static int update_channel_config(struct usb_skel *dev, u8 index) {
	struct ch_config cur_cfg;
	struct ep_data *ep_data;
	int result;
	
	ep_data = dev->ep_data_head->list;
	ep_data->setup.wIndex = index;
	result = own_usb_read(dev, ep_data, &ep_data->setup, (char*) &cur_cfg, false, sizeof(cur_cfg));
	
	if (result == sizeof(cur_cfg)) {
		ep_data = NULL;
		
		for (u8 ep_idx = 1u; ep_idx < dev->ep_count; ++ep_idx) {	//skip default control
			struct ep_data *ep_data_tmp = dev->ep_data_head->list + ep_idx;
			//search for matching endpoint corresponding to endpoint registered to channel
			if (ep_data_tmp->ep_num == cur_cfg.idx) {
				ep_data = ep_data_tmp;
				break;
			}
		}
		if (unlikely(!ep_data)) {
			dev_err(&dev->interface->dev, "Endpoint '%u' not found to match channel '%u' endpoint.",
				cur_cfg.idx, index);
			return -ENXIO;
		}
		
		dev_info(&dev->interface->dev, "Channel '%u' -> endpoint '%u'.", index, cur_cfg.idx);
		if (ep_data->setup.wIndex != U8_MAX) {
			dev_warn(&dev->interface->dev, "Endpoint '%u' already registered to channel '%u'.", cur_cfg.idx,
				ep_data->setup.wIndex);
		}
		ep_data->setup.wIndex = index;
		
		dev->sysfs_ch_cfg_pinbase[index] = cur_cfg.pinbase;
		dev->sysfs_ch_cfg_pincount[index] = cur_cfg.pincount;
		dev->sysfs_ch_cfg_rate[index] = cur_cfg.rate;
		
		result = 0;
	}
	else if (result >= 0) {
		result = -EINVAL;
	}
	
	return result;
}

//! Helper function to clear channel variables and from device.
//! @param[in] dev User data object.
//! @param[in] index Channel index, starting from 0.
static void clear_channel_config(struct usb_skel *dev, u8 index) {
	bool not_found = true;
	struct ch_config invalid_cfg = {								//set to invalid config to remove channel
		.idx = index,
		.pinbase = 0u,
		.pincount = 0u,
		.rate = 0u
	};
	struct usb_ctrlrequest setup = {
		.bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
		.bRequest = USB_REQ_SET_CONFIGURATION,
		.wValue = 0u,
		.wIndex = 0u
	};
	own_usb_write(dev, dev->ep_data_head->list, &setup, (const char*) &invalid_cfg, false,
		sizeof(invalid_cfg));
	
	for (u8 ep_idx = 1u; ep_idx < dev->ep_count; ++ep_idx) {		//skip default control
		struct ep_data *ep_data_tmp = dev->ep_data_head->list + ep_idx;
		if (ep_data_tmp->setup.wIndex == index) {
			ep_data_tmp->setup.wIndex = U8_MAX;
			not_found = false;
			break;
		}
	}
	if (unlikely(not_found)) {
		dev_warn(&dev->interface->dev, "No endpoint registered to cleared channel '%u'.", index);
	}
	
	dev->sysfs_ch_cfg_pinbase[index] = 0u;
	dev->sysfs_ch_cfg_pincount[index] = 0u;
	dev->sysfs_ch_cfg_rate[index] = 0u;
}

//! Helper function to validate new channel config and test for changes.
//! @param[in] dev User data object.
//! @param[in] index Channel index, starting from 0.
//! @param[in] pinbase New channel config for pin base index, starting from 0.
//! @param[in] pincount New channel config for pin count. Value must be either {1, 2, 4, 8}.
//! @param[in] rate New channel config for sampling rate, in Hz. Must be between 0 < x <= 125000000.
//! @return 0 if new config is valid and different from current config, &lt;0 if config is invalid, else 1.
static int validate_channel_config(struct usb_skel *dev, u8 index, u8 pinbase, u8 pincount, u32 rate) {
	if (pinbase >= 26u) {											//there're 26 GPIO pins on Pico
		dev_err(&dev->interface->dev, "Invalid pin base '%u' as channel config.", pinbase);
		return -EINVAL;
	}
	
	if ((pincount != 1u) && (pincount != 2u) && (pincount != 4u) && (pincount != 8u)) {
		dev_err(&dev->interface->dev, "Invalid pin count '%u' as channel config.", pincount);
		return -EINVAL;
	}
	
	if ((!rate) || (rate > 125000000u)) {							//125MHz as per default Pico system clock
		dev_err(&dev->interface->dev, "Invalid rate '%u' as channel config.", pincount);
		return -EINVAL;
	}
	
	return ((dev->sysfs_ch_cfg_pinbase[index] == pinbase) &&
		(dev->sysfs_ch_cfg_pincount[index] == pincount) && (dev->sysfs_ch_cfg_rate[index] == rate));
}

static ssize_t sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf) {
	struct usb_skel *dev = container_of(kobj, struct usb_skel, kobj);
	unsigned char index;
	ssize_t result;
	
	if (sscanf(attr->name, "ch%2hhu", &index) == 1) {
		if ((SYSFS_ATTR_CH_CFG_MIN <= index) && (index <= SYSFS_ATTR_CH_CFG_MAX)) {
			result = sysfs_emit(buf, "%u %u %u\n", dev->sysfs_ch_cfg_pinbase[index],
				dev->sysfs_ch_cfg_pincount[index], dev->sysfs_ch_cfg_rate[index]);
		}
		else {
			pr_err("sysfs channel index out-of-range: %u", index);
			result = -EBADF;
		}
	}
	else if (strcmp(attr->name, "chcount")) {
		pr_err("Unknown sysfs object: %s", attr->name);
		result = -EBADF;
	}
	else {
		result = sysfs_emit(buf, "%u\n", dev->sysfs_ch_count);
	}
	
	return result;
}

static ssize_t sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count) {
	struct usb_skel *dev = container_of(kobj, struct usb_skel, kobj);
	unsigned char index;
	
	if (sscanf(attr->name, "ch%2hhu", &index) == 1) {				//per-channel config
		if ((SYSFS_ATTR_CH_CFG_MIN <= index) && (index <= SYSFS_ATTR_CH_CFG_MAX)) {
			unsigned char pinbase, pincount;
			unsigned int rate;
			int result;
			
			result = sscanf(buf, "%2hhu %1hhu %u", &pinbase, &pincount, &rate);
			
			if (result == 3) {
				result = validate_channel_config(dev, index, pinbase, pincount, rate);
				
				if (result < 0) {
					return result;
				}
				
				if (!result) {
					struct ch_config cfg = {
						.idx = index,
						.pinbase = pinbase,
						.pincount = pincount,
						.rate = rate
					};
					struct usb_ctrlrequest setup = {
						.bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
						.bRequest = USB_REQ_SET_CONFIGURATION,
						.wValue = 0u,
						.wIndex = 0u
					};
					
					result = own_usb_write(dev, dev->ep_data_head->list, &setup, (const char*) &cfg,
						false, sizeof(cfg));
					if (result < 0) {
						return result;
					}
					result = 0;
				}
				
				if (!result) {
					dev->sysfs_ch_cfg_pinbase[index] = pinbase;
					dev->sysfs_ch_cfg_pincount[index] = pincount;
					dev->sysfs_ch_cfg_rate[index] = rate;
				}
			}
			else {
				dev_err(&dev->interface->dev, "Invalid channel configuration string %zu:%s", count, buf);
				return (result < 0) ? result : -EINVAL;
			}
		}
		else {
			dev_err(&dev->interface->dev, "sysfs channel index '%u' out-of-range.", index);
			return -EBADF;
		}
	}
	else if (strcmp(attr->name, "chcount")) {
		dev_err(&dev->interface->dev, "Unknown sysfs object: %s", attr->name);
		return -EBADF;
	}
	else {															//channel management
		unsigned char ch_count;
		
		if (sscanf(buf, "%2hhu", &ch_count) == 1) {
			if (ch_count > dev->sysfs_ch_count) {					//adding new channels
				struct ch_config valid_cfg = {
					.pincount = 1u,
					.rate = 1u
				};
				struct usb_ctrlrequest setup = {
					.bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
					.bRequest = USB_REQ_SET_CONFIGURATION,
					.wValue = 0u,
					.wIndex = 0u
				};
				
				if (unlikely(ch_count >= dev->ep_count)) {			//possible that endpoints are limited
					dev_err(&dev->interface->dev, "Channel count '%u' >= '%u'.", ch_count, dev->ep_count);
					return -EINVAL;
				}
				
				for (index = dev->sysfs_ch_count; index < ch_count; ++index) {
					int result;
					
					//try to read current channel config to check if it's already registered
					if (update_channel_config(dev, index)) {
						valid_cfg.idx = index;						//use valid default config to add channel
						valid_cfg.pinbase = index;
						result = own_usb_write(dev, dev->ep_data_head->list, &setup, (const char*) &valid_cfg,
							false, sizeof(valid_cfg));
						if (result < 0) {
							return result;
						}
						
						//need to update endpoint/channel-related variables for newly created channel
						result = update_channel_config(dev, index);
						if (result) {
							return result;
						}
					}
					else {
						dev_info(&dev->interface->dev, "Channel '%u' already registered.", index);
					}
					
					result = own_sysfs_param_setup(dev, index);
					if (result) {
						return result;
					}
					
					result = own_cdev_create_dev(dev, index);
					if (result) {
						return result;
					}
				}
				
				dev_info(&dev->interface->dev, "Setting channel count to %u.", index);
				dev->sysfs_ch_count = index;
			}
			else if (ch_count < dev->sysfs_ch_count) {				//removing some channels
				for (index = ch_count; index < dev->sysfs_ch_count; ++index) {
					clear_channel_config(dev, index);
					own_sysfs_param_cleanup(dev, index);
					own_cdev_delete_dev(dev, index);
				}
				
				dev->sysfs_ch_count = ch_count;
			}
		}
		else {
			dev_err(&dev->interface->dev, "Invalid channel count string: %s", buf);
			return -EINVAL;
		}
	}
	
	return count;
}

//! Sets up device sysfs parameter for either channel addition or when device is connected.
//! @param[in] dev User data object.
//! @param[in]] index Target object index in \ref usb_skel::sysfs_param_attrs array.
//! @return 0 if parameter file is created successfully.
static int own_sysfs_param_setup(struct usb_skel *dev, u8 index) {
	struct param_attr *attr = dev->sysfs_param_attrs + index;
	int result;
	
	if ((SYSFS_ATTR_CH_CFG_MIN <= index) && (index <= SYSFS_ATTR_CH_CFG_MAX)) {
		snprintf(attr->name, ARRAY_SIZE(attr->name), "ch%u", index);
	}
	else if (index == SYSFS_ATTR_CH_COUNT) {
		strscpy(attr->name, "chcount", ARRAY_SIZE(attr->name));
	}
	else {
		dev_err(&dev->interface->dev, "Invalid sysfs attribute index '%u'.", index);
		return -EBADF;
	}
	
	#ifdef CONFIG_DEBUG_LOCK_ALLOC
	lockdep_register_key(&attr->key);
	attr->attr.key = &attr->key;
	#endif
	attr->attr.mode = 0660u;
	attr->attr.name = attr->name;
	
	result = sysfs_create_file(&dev->kobj, &dev->sysfs_param_attrs[index].attr);
	if (result) {
		dev_err(&dev->interface->dev, "Error creating sysfs file '%s'.", dev->sysfs_param_attrs[index].name);
		return result;
	}
	
	return 0;
}

//! Prepares device sysfs parameter for either channel removal or device disconnection.
//! @param[in] dev User data object.
//! @param[in]] index Target object index in \ref usb_skel::sysfs_param_attrs array.
static void own_sysfs_param_cleanup(struct usb_skel *dev, u8 index) {
	struct param_attr *attr = dev->sysfs_param_attrs + index;
	
	sysfs_remove_file(&dev->kobj, &attr->attr);
	#ifdef CONFIG_DEBUG_LOCK_ALLOC
	lockdep_unregister_key(&attr->key);
	#endif
}
//**************************************************************************************

//! Handles open operation on USB device file.
static int skel_open(struct inode *inode, struct file *file) {
	return set_dev_to_file(iminor(inode), file);
}

//! Handles close operation on USB device file.
static int skel_release(struct inode *inode, struct file *file) {
	(void) inode;
	return unset_dev_from_file(file);
}

//! Deletes \ref usb_skel 'dev' object, allocated in \ref skel_probe, when reference count in
//! (\ref usb_skel::kobj) to the object reaches 0.
static void skel_delete(struct kobject *kobj) {
	struct usb_skel *dev = container_of(kobj, struct usb_skel, kobj);
	
	dev_info(&dev->interface->dev, "skel_delete.");
	if (dev->ep_data_head) {
		for (u8 idx = 0u; idx < dev->ep_count; ++idx) {
			ep_data_cleanup(dev->udev, dev->ep_data_head->list + idx);
		}
		kfree(dev->ep_data_head);
	}
	
	lockdep_unregister_key(&dev->disconnected_sem_key);
	
	usb_put_intf(dev->interface);
	usb_put_dev(dev->udev);
	kfree(dev);
}

//kobject-related setup
//**************************************************************************************
static const struct sysfs_ops kobject_sysfs_ops = {
	.show = sysfs_show,
	.store = sysfs_store
};

static const struct kobj_type ktype = {
	.release = skel_delete,
	.sysfs_ops = &kobject_sysfs_ops
};
//**************************************************************************************

//! Probably file operations supported by USB device file per device instance (there may be multiple devices
//! using same driver).
static const struct file_operations skel_fops = {
	.owner = THIS_MODULE,
	.open = skel_open,
	.release = skel_release
};

//! USB class driver info in order to get a minor number from the USB core, and to have the device registered
//! with the driver core.
static struct usb_class_driver skel_class = {
	.name = "rpi_host_low_%d",
	.fops = &skel_fops,
	.minor_base = USB_SKEL_MINOR_BASE
};

static int skel_probe(struct usb_interface *interface, const struct usb_device_id *id) {
	struct usb_skel *dev;
	int result;
	u8 bulk_ep_idx = 0u;
	
	(void) id;
	
	//allocate memory for our device state and initialize it
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		return -ENOMEM;
	}
	
	dev->interface = usb_get_intf(interface);
	kobject_init(&dev->kobj, &ktype);
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	
	lockdep_register_key(&dev->disconnected_sem_key);
	__init_rwsem(&dev->disconnected_sem, "disconnected_sem", &dev->disconnected_sem_key);
	spin_lock_init(&dev->op_lock);
	init_waitqueue_head(&dev->wait);
	
	//USB endpoint-related initializations
	//**********************************************************************************
	//search for bulk-in endpoints for endpoint >=1
	for (u8 idx = 0u; idx < interface->cur_altsetting->desc.bNumEndpoints; ++idx) {
		if (likely(usb_endpoint_is_bulk_in(&interface->cur_altsetting->endpoint[idx].desc))) {
			++(dev->ep_count);
		}
	}
	++(dev->ep_count);												//for default control endpoint
	
	dev->ep_data_head = kzalloc(sizeof(struct ep_data_head) + sizeof(struct ep_data) * dev->ep_count,
		GFP_KERNEL);
	if (!dev->ep_data_head) {
		result = -ENOMEM;
		goto err_1;
	}
	dev->ep_data_head->dev = dev;
	
	result = ep_data_setup(dev->udev, dev->ep_data_head->list, 0u, 0u);	//for default control endpoint
	if (unlikely(result)) {
		goto err_1;
	}
	for (u8 idx = 0u; idx < interface->cur_altsetting->desc.bNumEndpoints; ++idx) {
		const struct usb_endpoint_descriptor *epd = &interface->cur_altsetting->endpoint[idx].desc;
		
		if (likely(usb_endpoint_is_bulk_in(epd))) {
			result = ep_data_setup(dev->udev, dev->ep_data_head->list, ++bulk_ep_idx, usb_endpoint_num(epd));
			if (unlikely(result)) {
				goto err_1;
			}
		}
	}
	//**********************************************************************************
	
	//sysfs-related initializations
	//**********************************************************************************
	if (!interface->sysfs_files_created) {
		dev_err(&interface->dev, "USB interface doesn't creates sysfs files.");
		result = -ENOENT;
		goto err_1;
	}
	
	result = kobject_add(&dev->kobj, &dev->udev->dev.kobj, "parameters");
	if (result) {
		dev_err(&interface->dev, "Error setting kobj for device parameter(s).");
		goto err_1;
	}
	
	result = own_sysfs_param_setup(dev, SYSFS_ATTR_CH_COUNT);
	if (result) {
		goto err_2;
	}
	//**********************************************************************************
	
	//char device-related initialization
	result = own_cdev_setup_local(dev);
	if (result) {
		goto err_3;
	}
	
	//save our data pointer in this interface device
	usb_set_intfdata(interface, dev);
	
	//we can register the device now, as it is ready
	result = usb_register_dev(interface, &skel_class);
	if (result) {
		//something prevented us from registering this driver
		dev_err(&interface->dev, "Not able to get a minor for this device.");
		goto err_4;
	}
	
	//let the user know what node this device is now attached to
	dev_info(&interface->dev, "%s:%d device now attached.", MODULE_NAME, interface->minor);
	
	return 0;

err_4:
	usb_set_intfdata(interface, NULL);
err_3:
	own_cdev_cleanup_local(dev);
err_2:
	own_sysfs_param_cleanup(dev, SYSFS_ATTR_CH_COUNT);
err_1:
	kobject_put(&dev->kobj);
	
	return result;
}

static void skel_disconnect(struct usb_interface *interface) {
	struct usb_skel *dev = usb_get_intfdata(interface);
	const int minor = interface->minor;
	
	dev_info(&interface->dev, "cdev cleanup local.");
	own_cdev_cleanup_local(dev);									//char device cleanup
	
	dev_info(&interface->dev, "sysfs param cleanup.");
	for (u8 idx = 0u; idx < dev->sysfs_ch_count; ++idx) {			//sysfs cleanup
		own_sysfs_param_cleanup(dev, idx);
	}
	own_sysfs_param_cleanup(dev, SYSFS_ATTR_CH_COUNT);
	
	usb_set_intfdata(interface, NULL);
	usb_deregister_dev(interface, &skel_class);						//give back our minor
	
	down_write(&dev->disconnected_sem);								//prevent more I/O from starting
	dev->disconnected = true;
	up_write(&dev->disconnected_sem);
	
	kobject_put(&dev->kobj);
	
	dev_info(&interface->dev, "%s:%d now disconnected.", MODULE_NAME, minor);
}

static int skel_pre_reset(struct usb_interface *intf) {
	struct usb_skel *dev = usb_get_intfdata(intf);
	
	//lock and disable read/write operation across all device endpoints
	for(u8 index = 0u; index < dev->ep_count; ++index) {
		struct ep_data *ep_data = dev->ep_data_head->list + index;
		mutex_lock(&ep_data->op_mutex);								//wait for operation to stop
		usb_kill_urb(ep_data->urb);									//probably not needed
	}
	
	return 0;
}

static int skel_post_reset(struct usb_interface *intf) {
	struct usb_skel *dev = usb_get_intfdata(intf);
	
	//reenable back read/write operation, with last operation to be considered broken
	for(u8 index = 0u; index < dev->ep_count; ++index) {
		struct ep_data *ep_data = dev->ep_data_head->list + index;
		ep_data->last_err = -EPIPE;
		mutex_unlock(&ep_data->op_mutex);
	}
	
	return 0;
}

static struct usb_driver skel_driver = {
	.name =		MODULE_NAME,
	.probe =	skel_probe,
	.disconnect =	skel_disconnect,
	.pre_reset =	skel_pre_reset,
	.post_reset =	skel_post_reset,
	.id_table =	skel_table
};

//! Helper function to set \ref usb_skel object as \b file private data, usually when file is opened.
//! @param[in] minor Target USB interface minor number.
//! @param[out] file File object to be set its private data.
//! @return 0 if minor number matches device managed by this module and USB interface data is valid.
static int set_dev_to_file(int minor, struct file *file) {
	struct usb_interface *interface = usb_find_interface(&skel_driver, minor);
	struct usb_skel *dev;
	
	if (!interface) {
		pr_err("No such USB device with minor number '%d'.", minor);
		return -ENODEV;
	}
	
	dev = usb_get_intfdata(interface);
	if (!dev) {
		return -ENODEV;
	}
	
	//increment our usage count for the device
	kobject_get(&dev->kobj);
	file->private_data = dev;
	
	return 0;
}

//! Helper function to unset \ref usb_skel object from \b file private data, usually when file is closed.
//! @param[in] file File object to be unset its private data.
//! @return 0 if \b file private data is valid.
static int unset_dev_from_file(struct file *file) {
	struct usb_skel *dev = file->private_data;
	
	if (!dev) {
		return -ENODEV;
	}
	
	//decrement the count on our device
	kobject_put(&dev->kobj);
	
	return 0;
}

//! Called when loading this module into kernel.
//! @return 0 if no error has occurred.
static int __init own_module_init(void) {
	int result;
	
	pr_info("USB driver module initializing...");
	
	result = usb_register(&skel_driver);
	if (result) {
		pr_err("Error registering USB driver module with status %d.", result);
		goto err_1;
	}
	
	result = own_cdev_setup_global();								//cdev-related initializations
	if (result) {
		goto err_2;
	}
	
	return 0;
	
err_2:
	usb_deregister(&skel_driver);
err_1:
	return result;
}

//! Called when unloading this module from kernel.
static void __exit own_module_exit(void) {
	own_cdev_cleanup_global();
	
	usb_deregister(&skel_driver);
	
	pr_info("Exited.");
}

module_init(own_module_init);
module_exit(own_module_exit);

MODULE_LICENSE("GPL v2");
