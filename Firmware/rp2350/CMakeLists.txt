cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pov_wand C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(SEED_DIR ${CMAKE_CURRENT_LIST_DIR}/seed)
set(SEED_FILES
    ${SEED_DIR}/README.txt
    ${SEED_DIR}/settings.txt
    ${SEED_DIR}/message.txt
    ${SEED_DIR}/image.bmp
    ${SEED_DIR}/blank_template.bmp
)
set(SEED_C ${CMAKE_BINARY_DIR}/seed_files.c)
add_custom_command(
    OUTPUT ${SEED_C}
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/gen_seed.py ${SEED_DIR} ${SEED_C}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/gen_seed.py ${SEED_FILES}
    VERBATIM
)

add_executable(pov_wand
    src/main.c
    src/content.c
    src/tracker.c
    src/leds.c
    src/accel.c
    src/flash_disk.c
    src/diskio.c
    src/storage.c
    src/usb_wand.c
    src/log.c
    src/sleep.c
    src/report.c
    src/int_test.c
    ${SEED_C}
    ${CMAKE_CURRENT_LIST_DIR}/third_party/fatfs/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/third_party/fatfs/ffunicode.c
)

target_include_directories(pov_wand PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/third_party/fatfs
)

target_link_libraries(pov_wand
    pico_stdlib
    pico_double
    pico_unique_id
    hardware_spi
    hardware_gpio
    hardware_flash
    hardware_sync
    pico_low_power
    tinyusb_device
    tinyusb_board
)

target_link_options(pov_wand PRIVATE -Wl,-u,_printf_float)
pico_enable_stdio_usb(pov_wand 0)
pico_enable_stdio_uart(pov_wand 0)
pico_add_extra_outputs(pov_wand)

if(POV_BENCH)
    target_compile_definitions(pov_wand PRIVATE POV_BENCH=1)
endif()
