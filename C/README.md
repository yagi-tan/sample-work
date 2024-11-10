Folders with `bmp280_pio_*` names are implementations for communication between Raspberry Pi Pico and BMP280 temperature+pressure sensor chip through I2C or SPI interface. It's written for the purpose of learning embedded programming, thus here it uses Pico PIO instead of simpler I2C/SPI onboard pins/function (both ways are done, but only the former is put here).

# [bmp280_pio_i2c](./bmp280_pio_i2c)

I2C-based interface for Raspberry Pi Pico and BMP280 sensor. Refer to [original example](https://github.com/raspberrypi/pico-examples/tree/master/pio/i2c) for comparison.

# [bmp280_pio_spi](./bmp280_pio_spi)

4-wire SPI-based interface for Raspberry Pi Pico and BMP280 sensor. Refer to [original example](https://github.com/raspberrypi/pico-examples/tree/master/pio/spi) for comparison.

# [logic_analyser](./logic_analyser)

Function to sample GPIO pin(s) and record their result to be sent to user later, currently via UART with USB is in progress. Refer to [original example](https://github.com/raspberrypi/pico-examples/tree/master/pio/logic_analyser/logic_analyser.c) for comparison, mostly restructure to make setup (multiple pin groups with different data rate) and transport (data format during transfer) easier.

# [rpi4_poweroff](./rpi4_poweroff)

Simple program to power-off Raspberry Pi 4 by monitoring specific pin activation using [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git). Power-off is initiated by press-and-hold push button, connected to pin and 3.3V source, for more than 3 seconds.

# [usb_host_low](./usb_host_low)

Linux v6.1.66 kernel module (LKM) that is supposed to interact with logic analyser above. It provides character device files as data source to be read (one for each logic analyser channel) and sysfs attributes as control interface (set number of channels, each channel configuration). Based on [USB module sample](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/usb/usb-skeleton.c?h=v6.1.66) for USB parts, and [LKM programming guide](https://sysprog21.github.io/lkmpg/) for sysfs and character device file usage.

# [utilities](./utilities)

Due to mistake in preparing the hardware, the UART part of UART-to-USB adapter is actually serial UART instead of TTL while the Pico UART pins expect the latter, resulting in unusable first bit for each byte. `send_*` functions are used to deal with this.