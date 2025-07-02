# The Pi Pico RIA
add_executable(rp816)
pico_add_extra_outputs(rp816)
pico_set_program_name(rp816 "RP816 RIA")
# pico_set_program_version(rp816 "0.1")
pico_set_binary_type(rp816 copy_to_ram)

target_include_directories(rp816 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ria
)

target_compile_definitions(rp816 PRIVATE
    pico_flash_bank_get_storage_offset_func=lfs_get_bt_storage_offset
    LFS_NO_ASSERT=1
    LFS_NO_MALLOC=1
    LFS_NAME_MAX=16
    PICO_CORE1_STACK_SIZE=0x200
)

target_compile_options(rp816 PRIVATE
    -Wall -Wextra
)

target_link_libraries(rp816 PRIVATE
    pico_aon_timer
    pico_stdlib
    pico_multicore
    pico_rand
    pico_aon_timer
    hardware_dma
    hardware_pio
    hardware_spi
    hardware_i2c
    hardware_pwm
    hardware_interp
    hardware_flash
    tinyusb_host
)

pico_generate_pio_header(rp816
    ${CMAKE_CURRENT_LIST_DIR}/ria/sys/bus.pio
)

pico_generate_pio_header(rp816
    ${CMAKE_CURRENT_LIST_DIR}/ria/sys/aud.pio
)

target_sources(rp816 PRIVATE
    ria/main.c
    ria/str.c
    ria/api/api.c
    ria/api/clk.c
    ria/api/oem.c
    ria/mon/at.c
    ria/mon/fil.c
    ria/mon/hlp.c
    ria/mon/mon.c
    ria/mon/ram.c
    ria/mon/rom.c
    ria/mon/set.c
    ria/sys/aud.c
    ria/sys/bus.c
    ria/sys/cfg.c
    ria/sys/com.c
    ria/sys/cpu.c
    ria/sys/ext.c
    ria/sys/led.c
    ria/sys/lfs.c
    ria/sys/mdm.c
    ria/sys/mem.c
    ria/sys/out.c
    ria/sys/sys.c
    ria/sys/psram.c
    ria/term/color.c
    ria/term/font.c
    ria/term/term.c
    ria/cgia/cgia.c
    ria/cgia/cgia_encode.S
    ria/cgia/cgia_sprites.S
    ria/usb/hid.c
    ria/usb/kbd.c
    ria/usb/mou.c
    ria/usb/msc.c
    ria/usb/pad.c
    fatfs/ff.c
    fatfs/ffunicode.c
    littlefs/lfs.c
    littlefs/lfs_util.c
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
)

# Please change name for hardware forks.
# For release, set version string and set code page to 0.
set_property(TARGET rp816
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_NAME="X65 microcomputer"
    RP6502_CODE_PAGE=437
    RP6502_KEYBOARD=EN_US
    RP6502_MIN_PHI2=1000
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
