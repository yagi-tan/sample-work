# [hvac-simulator](./hvac-simulator)

This folder only contains a part of HVAC BACnet device simulator (server-side), so it won't be compilable. It's implemented for the purpose with learning latest C++ features at that time (before C++20), thus weird/over-verbose ways of coding.

# [serial](./serial)

This folder implements serial (TTY) device reader with options on how to display the characters and/or write them to file for handling jumbled data from Pico due to UART-to-USB adapter. Closely related to [logic-analyser](../C/logic_analyser) and [utilities](../C/utilities).

```sh
#input from adapter, disable output to console, write to 'sampling.bin', no timestamp
./serial -i /dev/ttyUSB0 -m 0 -o sampling.bin
```