# Setup the pico C SDK on the local machine
#!/bin/bash
# sudo apt install -y cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
set -e
if [ ! -d "pico-sdk" ]; then
	git clone https://github.com/raspberrypi/pico-sdk.git
fi
export PICO_SDK_PATH=$(pwd)/pico-sdk
if [ ! -f "pico_sdk_import.cmake" ]; then
	cp pico-sdk/external/pico_sdk_import.cmake .
fi
