# X65 does not use Pico2 WiFi
set(RP6502_RIA_W FALSE)


# The X65 NorthBridge (including RIA)

add_executable(x65_north)
pico_add_extra_outputs(x65_north)
pico_set_binary_type(x65_north copy_to_ram)
pico_set_program_name(x65_north "X65 NorthBridge")

target_include_directories(x65_north PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/north
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_compile_definitions(x65_north PRIVATE
    PLL_SYS_REFDIV=1
    PLL_SYS_VCO_FREQ_HZ=1344000000
    PLL_SYS_POSTDIV1=6
    PLL_SYS_POSTDIV2=1
    SYS_CLK_HZ=224000000
    SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1
    SYS_CLK_VREG_VOLTAGE_MIN=VREG_VOLTAGE_1_10
    pico_flash_bank_get_storage_offset_func=lfs_get_bt_storage_offset
    PICO_FLASH_ASSUME_CORE1_SAFE=1
    LFS_NO_ASSERT=1
    LFS_NO_MALLOC=1
    LFS_NAME_MAX=16
)

target_compile_options(x65_north PRIVATE
    -Wall -Wextra
)

target_link_libraries(x65_north PRIVATE
    pico_aon_timer
    pico_stdlib
    pico_multicore
    pico_rand
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_flash
    tinyusb_host
    pico_btstack_ble
)

pico_generate_pio_header(x65_north
    ${CMAKE_CURRENT_LIST_DIR}/north/cpu.pio
)

pico_generate_pio_header(x65_north
    ${CMAKE_CURRENT_LIST_DIR}/pix.pio
)

target_sources(x65_north PRIVATE
    fatfs/ff.c
    fatfs/ffunicode.c
    littlefs/lfs.c
    littlefs/lfs_util.c
    north/main.c
    north/api/api.c
    north/api/clk.c
    north/api/dir.c
    north/api/oem.c
    north/api/rng.c
    north/api/std.c
    north/hid/hid.c
    north/hid/kbd.c
    north/hid/mou.c
    north/hid/pad.c
    north/mon/at.c
    north/mon/fil.c
    north/mon/hlp.c
    north/mon/mon.c
    north/mon/ram.c
    north/mon/rom.c
    north/mon/set.c
    north/mon/str.c
    north/mon/tst.c
    north/sys/cfg.c
    north/sys/com.c
    north/sys/cpu.c
    north/sys/led.c
    north/sys/lfs.c
    north/sys/mem.c
    north/sys/pix.c
    north/sys/ria.c
    north/sys/rln.c
    north/sys/sys.c
    north/sys/vga.c
    north/usb/msc.c
    north/usb/usb.c
    north/usb/xin.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)


# The X65 SouthBridge (including CGIA)

add_executable(x65_south)
pico_add_extra_outputs(x65_south)
pico_set_program_name(x65_south "X65 SouthBridge")
pico_set_binary_type(x65_south copy_to_ram)

target_compile_options(x65_south PRIVATE
    -Wall -Wextra
)

target_include_directories(x65_south PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/south
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_sources(x65_south PRIVATE
    south/main.c
    south/cgia/cgia.c
    south/cgia/cgia_encode.S
    south/cgia/cgia_sprites.S
    south/sys/com.c
    south/sys/led.c
    south/sys/out.c
    south/sys/pix.c
    south/sys/sys.c
    south/term/color.c
    south/term/font.c
    south/term/term.c
    south/usb/cdc.c
    south/usb/descriptors.c
    south/usb/usb.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

target_link_libraries(x65_south PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_interp
    tinyusb_device
)

pico_generate_pio_header(x65_south
    ${CMAKE_CURRENT_LIST_DIR}/pix.pio
)

target_compile_definitions(x65_south PRIVATE
    PLL_SYS_REFDIV=1
    PLL_SYS_VCO_FREQ_HZ=1344000000
    PLL_SYS_POSTDIV1=4
    PLL_SYS_POSTDIV2=1
    SYS_CLK_HZ=336000000
    SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1
    SYS_CLK_VREG_VOLTAGE_MIN=VREG_VOLTAGE_1_20
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
)


# The X65 Custom Sound Processor

add_executable(x65_xcsp)
pico_add_extra_outputs(x65_xcsp)
pico_set_program_name(x65_xcsp "X65 Custom Sound Processor")
pico_set_binary_type(x65_xcsp copy_to_ram)

target_compile_options(x65_xcsp PRIVATE
    -Wall -Wextra
)

target_include_directories(x65_xcsp PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/xcsp
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_sources(x65_xcsp PRIVATE
    xcsp/main.c
    xcsp/sys/aud.c
    xcsp/sys/led.c
    xcsp/sys/sys.c
    xcsp/usb/descriptors.c
    xcsp/usb/usb.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

target_link_libraries(x65_xcsp PRIVATE
    pico_stdlib
    pico_multicore
    pico_rand
    hardware_pio
    hardware_dma
    hardware_interp
    hardware_i2c
    hardware_spi
    tinyusb_device
)

pico_generate_pio_header(x65_xcsp
    ${CMAKE_CURRENT_LIST_DIR}/xcsp/aud.pio
)

target_compile_definitions(x65_xcsp PRIVATE
    PLL_SYS_REFDIV=1
    PLL_SYS_VCO_FREQ_HZ=1344000000
    PLL_SYS_POSTDIV1=4
    PLL_SYS_POSTDIV2=1
    SYS_CLK_HZ=336000000
    SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1
    SYS_CLK_VREG_VOLTAGE_MIN=VREG_VOLTAGE_1_30
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
)


# Project defines available to both Pi Picos.
# Please change name for hardware forks.
# For release, set version string and set code page to 0.

set_property(TARGET x65_north x65_south x65_xcsp
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_NAME="X65 microcomputer"
    RP6502_VERSION=""
    RP6502_CODE_PAGE=437
    RP6502_KEYBOARD=EN_US
    RP6502_EXFAT=0
)


# Add a custom command that produces version.c, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build
ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.h
           ${CMAKE_CURRENT_BINARY_DIR}/_version.h
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)
