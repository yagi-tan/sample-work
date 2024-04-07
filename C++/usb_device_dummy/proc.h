#ifndef PROC_H
#define PROC_H

#include <linux/usb/ch9.h>

#include <string_view>

bool startProc(std::string_view device, std::string_view driver, usb_device_speed speed);
void stopProc();

#endif
