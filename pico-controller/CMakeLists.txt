cmake_minimum_required(VERSION 3.13)

# Initialize the Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(thermo_controller C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable
add_executable(thermo_controller
    controller.c
)

# Link libraries
target_link_libraries(thermo_controller
    pico_stdlib
    hardware_gpio
    hardware_i2c
)

# Enable USB output for debug, disable UART
pico_enable_stdio_usb(thermo_controller 1)
pico_enable_stdio_uart(thermo_controller 0)

# Create UF2 file for drag-and-drop programming
pico_add_extra_outputs(thermo_controller)
