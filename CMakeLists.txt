# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(AutoCatFeeder C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

message("Running makefsdata python script")
execute_process(COMMAND
    py makefsdata.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

# Add executable. Default name is the project name, version 0.1
add_executable(AutoCatFeeder 
    AutoCatFeeder.cpp
)

pico_set_program_name(AutoCatFeeder "AutoCatFeeder")
pico_set_program_version(AutoCatFeeder "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(AutoCatFeeder 0)
pico_enable_stdio_usb(AutoCatFeeder 1)

# Add the standard library to the build
target_link_libraries(AutoCatFeeder
        pico_stdlib)

# Add submodules to my project
add_subdirectory(extern/hx711-pico-c)

# Add the standard include files to the build
target_include_directories(AutoCatFeeder PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        extern/hx711-pico-c/include
)

# Add any user requested libraries
target_link_libraries(AutoCatFeeder 
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_http
        pico_stdlib
        hx711-pico-c
        pico_multicore
        )

# Need to generate UF2 file for upload to RP2040
pico_add_extra_outputs(AutoCatFeeder)

