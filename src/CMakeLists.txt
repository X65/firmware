# The Pi Pico RIA
add_executable(rp6502_ria)
pico_add_extra_outputs(rp6502_ria)
pico_set_program_name(rp6502_ria "RP65816 RIA")
# pico_set_program_version(rp6502_ria "0.1")
pico_set_binary_type(rp6502_ria copy_to_ram)

pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(rp6502_ria slower_boot2)

target_include_directories(rp6502_ria PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
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
    pico_aon_timer
    hardware_pio
    hardware_dma
    hardware_flash
    tinyusb_board
    tinyusb_host
)

pico_generate_pio_header(rp6502_ria
    ${CMAKE_CURRENT_LIST_DIR}/ria/sys/bus.pio
)

pico_generate_pio_header(rp6502_ria
    ${CMAKE_CURRENT_LIST_DIR}/ria/sys/led.pio
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
    ria/sys/bus.c
    ria/sys/cfg.c
    ria/sys/com.c
    ria/sys/cpu.c
    ria/sys/led.c
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

# Please change name for hardware forks.
# For release, set version string and set code page to 0.
set_property(TARGET rp6502_ria
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
