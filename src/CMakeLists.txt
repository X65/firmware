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

pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(rp6502_ria slower_boot2)

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
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
)


# The Pi Pico VPU

add_subdirectory(picodvi/software/libdvi)

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
    vpu/sys/out.c
    vpu/sys/pix.c
    vpu/sys/ram.c
    vpu/sys/std.c
    vpu/term/font.c
    vpu/term/term.c
    vpu/cgia/cgia.c
    vpu/cgia/tmds_encode_cgia.S
    vpu/usb/cdc.c
    vpu/usb/descriptors.c
    vpu/usb/serno.c
    picodvi/software/apps/colour_terminal/tmds_encode_font_2bpp.S
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
)

target_link_libraries(rp6502_vpu PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    libdvi
    tinyusb_vpu
)

pico_generate_pio_header(rp6502_vpu
    ${CMAKE_CURRENT_LIST_DIR}/vpu/sys/ria.pio
)

pico_generate_pio_header(rp6502_vpu
    ${CMAKE_CURRENT_LIST_DIR}/vpu/sys/ram.pio
)

target_compile_definitions(rp6502_vpu PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    DVI_VERTICAL_REPEAT=2
    DVI_SYMBOLS_PER_WORD=2
    PICO_CORE1_STACK_SIZE=0x200
	# DVI_N_TMDS_BUFFERS=8
    # TMDS_FULLRES_NO_DC_BALANCE=1
    # TMDS_ENCODE_UNROLL=8
)


# Project defines available to both Pi Picos.
# Please change name for hardware forks.
# For release, set version string and set code page to 0.

set_property(TARGET rp6502_ria rp6502_vpu
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_NAME="Picocomputer 65816"
    RP6502_CODE_PAGE=850
    RP6502_KEYBOARD=EN_US
    RP6502_MIN_PHI2=500
    RP6502_MAX_PHI2=8000
    RP6502_EXFAT=0
)

# Add a custom command that produces version.c, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build
ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.c
           ${CMAKE_CURRENT_BINARY_DIR}/_version.c
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)
