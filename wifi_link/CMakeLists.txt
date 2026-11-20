cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wifi_link C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(wifi_link
    main.c
    lcd.c
)

target_include_directories(wifi_link PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# Code and lwIP live in the game flash region (~896 KiB); RAM is shared with
# the network stack, so give the main stack a bit more room than the default.
target_compile_definitions(wifi_link PRIVATE PICO_STACK_SIZE=0x1000)

target_link_libraries(wifi_link
    pico_stdlib
    pico_cyw43_arch_lwip_poll
    hardware_adc
    hardware_flash
    hardware_spi
)

pico_enable_stdio_usb(wifi_link 1)
pico_enable_stdio_uart(wifi_link 0)
pico_add_extra_outputs(wifi_link)
