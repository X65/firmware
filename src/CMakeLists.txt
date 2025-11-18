# X65 does not use Pico2 WiFi
set(RP6502_RIA_W FALSE)


# The X65 RIA

add_executable(x65_ria)
pico_add_extra_outputs(x65_ria)
pico_set_binary_type(x65_ria copy_to_ram)
pico_set_program_name(x65_ria "X65 RIA")

target_include_directories(x65_ria PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ria
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_compile_definitions(x65_ria PRIVATE
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

target_compile_options(x65_ria PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Release>:-Os>
)

target_link_libraries(x65_ria PRIVATE
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

pico_generate_pio_header(x65_ria
    ${CMAKE_CURRENT_LIST_DIR}/ria/cpu.pio
)

pico_generate_pio_header(x65_ria
    ${CMAKE_CURRENT_LIST_DIR}/ria/ria.pio
)

target_sources(x65_ria PRIVATE
    fatfs/ff.c
    fatfs/ffunicode.c
    littlefs/lfs.c
    littlefs/lfs_util.c
    ria/main.c
    ria/api/api.c
    ria/api/clk.c
    ria/api/dir.c
    ria/api/oem.c
    ria/api/rng.c
    ria/api/std.c
    ria/hid/hid.c
    ria/hid/kbd.c
    ria/hid/mou.c
    ria/hid/pad.c
    ria/mon/at.c
    ria/mon/fil.c
    ria/mon/hlp.c
    ria/mon/mon.c
    ria/mon/ram.c
    ria/mon/rom.c
    ria/mon/set.c
    ria/mon/str.c
    ria/mon/tst.c
    ria/sys/cfg.c
    ria/sys/com.c
    ria/sys/cpu.c
    ria/sys/led.c
    ria/sys/lfs.c
    ria/sys/mem.c
    ria/sys/pix.c
    ria/sys/ria.c
    ria/sys/rln.c
    ria/sys/sys.c
    ria/sys/vga.c
    ria/usb/msc.c
    ria/usb/usb.c
    ria/usb/xin.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)


# The X65 VPU

add_executable(x65_vpu)
pico_add_extra_outputs(x65_vpu)
pico_set_program_name(x65_vpu "X65 VPU")
pico_set_binary_type(x65_vpu copy_to_ram)

target_compile_options(x65_vpu PRIVATE
    -Wall -Wextra
)

target_include_directories(x65_vpu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/vpu
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_sources(x65_vpu PRIVATE
    vpu/main.c
    vpu/cgia/cgia.c
    vpu/cgia/cgia_encode.S
    vpu/cgia/cgia_sprites.S
    vpu/sys/com.c
    vpu/sys/led.c
    vpu/sys/out.c
    vpu/sys/pix.c
    vpu/sys/sys.c
    vpu/term/color.c
    vpu/term/font.c
    vpu/term/term.c
    vpu/usb/cdc.c
    vpu/usb/descriptors.c
    vpu/usb/usb.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

target_link_libraries(x65_vpu PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_interp
    tinyusb_device
)

pico_generate_pio_header(x65_vpu
    ${CMAKE_CURRENT_LIST_DIR}/vpu/vpu.pio
)

target_compile_definitions(x65_vpu PRIVATE
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


# The X65 SPU

add_executable(x65_spu)
pico_add_extra_outputs(x65_spu)
pico_set_program_name(x65_spu "X65 SPU")
pico_set_binary_type(x65_spu copy_to_ram)

target_compile_options(x65_spu PRIVATE
    -Wall -Wextra
)

target_include_directories(x65_spu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/spu
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
)

target_sources(x65_spu PRIVATE
    spu/main.c
    spu/sys/aud.c
    spu/sys/led.c
    spu/sys/sys.c
    spu/usb/descriptors.c
    spu/usb/usb.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

target_link_libraries(x65_spu PRIVATE
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

pico_generate_pio_header(x65_spu
    ${CMAKE_CURRENT_LIST_DIR}/spu/aud.pio
)

target_compile_definitions(x65_spu PRIVATE
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

set_property(TARGET x65_ria x65_vpu x65_spu
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
