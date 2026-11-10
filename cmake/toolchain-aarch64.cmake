# Cross-compile for the handheld's chip (64-bit ARM Linux).
#
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
#         -DAPOTHECA_BUILD_APP=OFF
#
# The app also needs the handheld's own SDL2. Point CMAKE_SYSROOT at a copy of
# the firmware's root filesystem (or its SDK) to build it:
#
#   -DCMAKE_SYSROOT=/path/to/firmware/sysroot -DAPOTHECA_BUILD_APP=ON

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Tuned for the Cortex-A53 cores in the Allwinner H700.
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a53")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
