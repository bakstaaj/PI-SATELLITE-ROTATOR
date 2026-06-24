# Historical/reference ARMv6 toolchain file.
#
# The original Pi Zero W release package is intentionally built inside the
# ARMv6 Raspberry-Pi-compatible Docker target stage instead of using Debian's
# generic arm-linux-gnueabihf cross compiler. Debian armhf is ARMv7/Thumb-2
# oriented and produced binaries that segfaulted on armv6l.
#
# This file is retained for reference, but scripts/build-rpi-zero-32.sh now
# uses the Dockerfile rpi-zero-32-cross-build target.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(CMAKE_C_FLAGS_INIT "-march=armv6 -marm -mfpu=vfp -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv6 -marm -mfpu=vfp -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
