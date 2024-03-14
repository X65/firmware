# TinyUSB detatched from Pico SDK so we can use latest

file(GLOB_RECURSE TINY_USB_SRC_FILES ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src/*.c)

add_library(tinyusb_ria STATIC EXCLUDE_FROM_ALL)
target_include_directories(tinyusb_ria PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    ria
)
target_link_libraries(tinyusb_ria PUBLIC
    pico_stdlib
)
target_sources(tinyusb_ria PRIVATE
    ${TINY_USB_SRC_FILES}
)

add_library(tinyusb_vpu STATIC EXCLUDE_FROM_ALL)
target_include_directories(tinyusb_vpu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    vpu
)
target_link_libraries(tinyusb_vpu PUBLIC
    pico_stdlib
)
target_sources(tinyusb_vpu PRIVATE
    ${TINY_USB_SRC_FILES}
)


# The Pi Pico RIA

add_executable(rp6502_ria)
pico_add_extra_outputs(rp6502_ria)
pico_set_program_name(rp6502_ria "RP6502 RIA")
pico_set_binary_type(rp6502_ria copy_to_ram)

target_include_directories(rp6502_ria PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    ria
)

target_compile_definitions(rp6502_ria PRIVATE
    LFS_NO_MALLOC=1
    LFS_NAME_MAX=16
)

target_compile_options(rp6502_ria PRIVATE
  -Wall -Wextra
)

target_link_libraries(rp6502_ria PRIVATE
    pico_stdlib
    pico_multicore
    pico_rand
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_rtc
    tinyusb_ria
)

pico_generate_pio_header(rp6502_ria
    ${CMAKE_CURRENT_LIST_DIR}/ria/sys/mem.pio
)

pico_generate_pio_header(rp6502_ria
    ${CMAKE_CURRENT_LIST_DIR}/ria/sys/pix.pio
)

target_sources(rp6502_ria PRIVATE
    ria/main.c
    ria/str.c
    ria/api/clk.c
    ria/mon/fil.c
    ria/mon/hlp.c
    ria/mon/mon.c
    ria/mon/ram.c
    ria/mon/rom.c
    ria/mon/set.c
    ria/sys/cfg.c
    ria/sys/com.c
    ria/sys/cpu.c
    ria/sys/lfs.c
    ria/sys/mem.c
    ria/sys/ria.c
    ria/sys/sys.c
    ria/usb/hid_ps4.c
    ria/usb/hid.c
    ria/usb/kbd.c
    ria/usb/mou.c
    ria/usb/msc.c
    ria/usb/usb.c
    fatfs/ff.c
    fatfs/ffunicode.c
    littlefs/lfs.c
    littlefs/lfs_util.c
)


# The Pi Pico VGA

add_executable(rp6502_vpu)
pico_add_extra_outputs(rp6502_vpu)
pico_set_program_name(rp6502_vpu "RP6502 VGA")
pico_set_binary_type(rp6502_vpu copy_to_ram)

target_include_directories(rp6502_vpu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    vpu
)

target_sources(rp6502_vpu PRIVATE
    vpu/main.c
    vpu/sys/led.c
    vpu/sys/pix.c
    vpu/sys/std.c
    vpu/sys/vga.c
    vpu/term/font.c
    vpu/term/term.c
    vpu/usb/cdc.c
    vpu/usb/descriptors.c
    vpu/usb/serno.c
)

target_link_libraries(rp6502_vpu PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_scanvideo_dpi
    tinyusb_vpu
)

pico_generate_pio_header(rp6502_vpu
    ${CMAKE_CURRENT_LIST_DIR}/vpu/sys/ria.pio
)

target_compile_definitions(rp6502_vpu PRIVATE
    PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=323
    PICO_SCANVIDEO_COLOR_PIN_BASE=6
    PICO_SCANVIDEO_SYNC_PIN_BASE=26
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)


# Project defines available to both Pi Picos.
# Please change name for hardware forks.
# For release, set version string and set code page to 0.

set_property(TARGET rp6502_ria rp6502_vpu
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_NAME="Picocomputer 65816"
    RP6502_VERSION=""
    RP6502_CODE_PAGE=850
    RP6502_KEYBOARD=EN_US
    RP6502_MIN_PHI2=500
    RP6502_MAX_PHI2=8000
    RP6502_EXFAT=0
)
