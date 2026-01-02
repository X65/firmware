# The X65 NorthBridge (including RIA)
set(RP6502_RIA_W TRUE)

add_executable(x65_north)
pico_add_extra_outputs(x65_north)
pico_set_binary_type(x65_north copy_to_ram)
target_compile_definitions(x65_north PRIVATE RP6502_RIA_W=1)
target_link_libraries(x65_north PRIVATE pico_cyw43_arch_lwip_poll pico_btstack_cyw43)
pico_set_program_name(x65_north "X65 NorthBridge")

target_include_directories(x65_north PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/north
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_compile_definitions(x65_north PRIVATE
    CYW43_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/north/cyw43_config.h"
    PLL_SYS_REFDIV=1
    PLL_SYS_VCO_FREQ_HZ=1344000000
    PLL_SYS_POSTDIV1=4
    PLL_SYS_POSTDIV2=1
    SYS_CLK_HZ=336000000
    SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1
    SYS_CLK_VREG_VOLTAGE_MIN=VREG_VOLTAGE_1_20
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
    north/mon/fil.c
    north/mon/hlp.c
    north/mon/mon.c
    north/mon/ram.c
    north/mon/rom.c
    north/mon/set.c
    north/mon/str.c
    north/mon/tst.c
    north/net/ble.c
    north/net/cmd.c
    north/net/cyw.c
    north/net/mdm.c
    north/net/ntp.c
    north/net/tel.c
    north/net/wfi.c
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
    north/sys/vpu.c
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

pico_generate_pio_header(x65_south
    ${CMAKE_CURRENT_LIST_DIR}/pix.pio
)

pico_generate_pio_header(x65_south
    ${CMAKE_CURRENT_LIST_DIR}/south/rgb.pio
)

target_sources(x65_south PRIVATE
    south/main.c
    south/cgia/cgia.c
    south/cgia/cgia_encode.S
    south/cgia/cgia_sprites.S
    south/sys/buz.c
    south/sys/com.c
    south/sys/ext.c
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
    hardware_i2c
    hardware_pwm
    tinyusb_device
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


# The X65 SGU-1 - Sound Generator Unit 1

add_executable(x65_sgu)
pico_add_extra_outputs(x65_sgu)
pico_set_program_name(x65_sgu "X65 SGU-1")
pico_set_binary_type(x65_sgu copy_to_ram)

target_compile_options(x65_sgu PRIVATE
    -Wall -Wextra
)

target_include_directories(x65_sgu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/audio
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

pico_generate_pio_header(x65_sgu
    ${CMAKE_CURRENT_LIST_DIR}/audio/aud.pio
)

pico_generate_pio_header(x65_sgu
    ${CMAKE_CURRENT_LIST_DIR}/south/rgb.pio
)

target_sources(x65_sgu PRIVATE
    audio/main.c
    audio/su/su.c
    audio/sys/aud.c
    audio/sys/com.c
    audio/sys/led.c
    audio/sys/sys.c
    audio/usb/cdc.c
    audio/usb/descriptors.c
    audio/usb/usb.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

target_link_libraries(x65_sgu PRIVATE
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

target_compile_definitions(x65_sgu PRIVATE
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

set_property(TARGET x65_north x65_south x65_sgu
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_NAME="X65 microcomputer"
    RP6502_VERSION=""
    RP6502_CODE_PAGE=0
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
