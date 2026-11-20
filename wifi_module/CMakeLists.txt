cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wifi_module C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(wifi_module
    wifi_module.c
)

target_include_directories(wifi_module PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# No crt0 and no pico-sdk runtime init: the launcher starts this on core 1.
target_compile_definitions(wifi_module PRIVATE
    PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1
    PICO_STACK_SIZE=0
    PICO_CORE1_STACK_SIZE=0
    PICO_PANIC_FUNCTION=wifi_panic
)

# RAM lives in the reserved 36 KB window; core 1 has its own stack in SCRATCH_Y.
# The header goes right after the 0xC0-byte vector table.
target_link_options(wifi_module PRIVATE
    "LINKER:--defsym=RAM_ORIGIN=0x2003A000"
    "LINKER:--defsym=RAM_LENGTH=0x6000"
    "LINKER:--undefined=wifi_module_entry"
    "LINKER:--undefined=wifi_hdr"
)

target_link_libraries(wifi_module
    pico_stdlib
    pico_cyw43_arch_lwip_poll
)

pico_enable_stdio_uart(wifi_module 0)
pico_enable_stdio_usb(wifi_module 0)
pico_add_extra_outputs(wifi_module)
