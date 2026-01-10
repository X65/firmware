/*
 * Copyright (c) 2025 Rumbledethumps
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mon/hlp.h"
#include "api/clk.h"
#include "hid/kbd.h"
#include "mon/mon.h"
#include "mon/rom.h"
#include "mon/str.h"
#include "net/cyw.h"
#include <pico.h>
#include <string.h>

#if defined(DEBUG_RIA_MON) || defined(DEBUG_RIA_MON_HLP)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

#define STR_PHI2_MIN_MAX STRINGIFY(CPU_PHI2_MIN_KHZ) "-" STRINGIFY(CPU_PHI2_MAX_KHZ)

#define X(name, value) \
    static const char __in_flash(STRINGIFY(name)) name[] = value;

X(STR_POSIX, "POSIX")
X(STR_END, "END")
X(STR_HELP, "HELP")
X(STR_H, "H")
X(STR_QUESTION_MARK, "?")
X(STR_STATUS, "STATUS")
X(STR_SET, "SET")
X(STR_LS, "LS")
X(STR_DIR, "DIR")
X(STR_CD, "CD")
X(STR_CHDIR, "CHDIR")
X(STR_MKDIR, "MKDIR")
X(STR_LOAD, "LOAD")
X(STR_INFO, "INFO")
X(STR_INSTALL, "INSTALL")
X(STR_REMOVE, "REMOVE")
X(STR_REBOOT, "REBOOT")
X(STR_RESET, "RESET")
X(STR_UPLOAD, "UPLOAD")
X(STR_UNLINK, "UNLINK")
X(STR_BINARY, "BINARY")
X(STR_PHI2, "PHI2")
X(STR_BOOT, "BOOT")
X(STR_TZ, "TZ")
X(STR_KB, "KB")
X(STR_CP, "CP")
X(STR_RF, "RF")
X(STR_RFCC, "RFCC")
X(STR_SSID, "SSID")
X(STR_PASS, "PASS")
X(STR_BLE, "BLE")
X(STR_ABOUT, "ABOUT")
X(STR_CREDITS, "CREDITS")
X(STR_SYSTEM, "SYSTEM")
X(STR_0, "0")
X(STR_0000, "0000")
X(STR_0_COLON, "0:")
X(STR_1_COLON, "1:")
X(STR_2_COLON, "2:")
X(STR_3_COLON, "3:")
X(STR_4_COLON, "4:")
X(STR_5_COLON, "5:")
X(STR_6_COLON, "6:")
X(STR_7_COLON, "7:")
X(STR_8_COLON, "8:")
X(STR_9_COLON, "9:")

X(STR_HELP_HELP,
  "Commands:\n"
  "HELP (command|rom)  - This help or expanded help for command or rom.\n"
  "HELP ABOUT|SYSTEM   - About includes credits. System for general usage.\n"
  "STATUS              - Show status of system and connected devices.\n"
  "SET (attr) (value)  - Change or show settings.\n"
  "LS (dir|drive)      - List contents of directory.\n"
  "CD (dir)            - Change or show current directory.\n"
  "(USB)0:             - USB0:-USB7: Change current USB drive.\n"
  "LOAD file           - Load ROM file. Start if contains reset vector.\n"
  "INFO file           - Show help text, if any, contained in ROM file.\n"
  "INSTALL file        - Install ROM file on RIA.\n"
  "rom                 - Load and start an installed ROM.\n"
  "REMOVE rom          - Remove ROM from RIA.\n"
  "REBOOT              - Reboot the RIA. Will load selected boot ROM.\n"
  "RESET               - Start CPU at current reset vector ($FFFC).\n"
  "MKDIR dir           - Make a new directory.\n"
  "UNLINK file|dir     - Delete a file or empty directory.\n"
  "UPLOAD file         - Write file. Binary chunks follow.\n"
  "BINARY addr len crc - Write memory. Binary data follows.\n"
  "MEMTEST             - Test PSRAM memory.\n"
  "0000 (00 00 ...)    - Read or write memory.\n")

X(STR_HELP_SET,
  "Settings:\n"
  "HELP SET attr       - Show information about a setting.\n"
  "SET PHI2 (kHz)      - Query or set PHI2 speed. This is the 6502 clock.\n"
  "SET BOOT (rom|-)    - Select ROM to boot from cold start. \"-\" for none.\n"
  "SET TZ (tz)         - Query or set time zone.\n"
  "SET KB (layout)     - Query or set keyboard layout.\n"
  "SET CP (cp)         - Query or set code page.\n"
#ifdef RP6502_RIA_W
  "SET RF (0|1)        - Disable or enable radio.\n"
  "SET RFCC (cc|-)     - Set country code for RF devices. \"-\" for worldwide.\n"
  "SET SSID (ssid|-)   - Set SSID for WiFi. \"-\" for none.\n"
  "SET PASS (pass|-)   - Set password for WiFi. \"-\" for none.\n"
  "SET BLE (0|1|2)     - Disable or enable Bluetooth LE. 2 enables pairing.\n"
#endif
  "")

// Note that BTstack HID descriptor parsing is used for non-W builds.
X(STR_HELP_ABOUT,
  "X65 microcomputer - Copyright (c) 2024-2026 Tomasz Sterna.\n"
  "Picocomputer 6502 - Copyright (c) 2025 Rumbledethumps.\n"
  "     Pi Pico SDKs - Copyright (c) 2020 Raspberry Pi (Trading) Ltd.\n"
  "      Tiny printf - Copyright (c) 2014-2019 Marco Paland, PALANDesign.\n"
  "          TinyUSB - Copyright (c) 2018 hathach (tinyusb.org)\n"
  "          BTstack - Copyright (c) 2009 BlueKitchen GmbH\n"
  "            FatFs - Copyright (c) 20xx ChaN.\n"
  "         littlefs - Copyright (c) 2022 The littlefs authors.\n"
  "                    Copyright (c) 2017 Arm Limited.\n"
#ifdef RP6502_RIA_W
  "   CYW43xx driver - Copyright (c) 2019-2022 George Robotics Pty Ltd.\n"
  "             lwIP - Copyright (c) 2001-2002 Swedish Institute of\n"
  "                                            Computer Science.\n"
#endif
  "")

X(STR_HELP_SYSTEM,
  "The X65 does not use a traditional parallel ROM like a 27C64 or\n"
  "similar. Instead, this monitor is used to prepare the CPU RAM with software\n"
  "that would normally be on a ROM chip. The CPU is currently in-reset right\n"
  "now; the RESB line is low. What you are seeing is coming from the RP-RIA.\n"
  "You can return to this monitor at any time by pressing CTRL-ALT-DEL or sending\n"
  "a break to the serial port. Since these signals are handled by the RP-RIA,\n"
  "they will always stop the CPU, even when crashed. This monitor can do scripted\n"
  "things that are useful for developing software. It also provides interactive\n"
  "commands like typing a hex address to see the corresponding RAM value:\n"
  "]0200\n"
  "000200  DA DA DA DA DA DA DA DA  DA DA DA DA DA DA DA DA  |................|\n"
  "The 16MB of memory is accessible from $000000 to $FFFFFF.\n"
  "Request a range of memory with the \"-\" character:\n"
  "]10000-100FF\n"
  "You can also set memory. For example, to set the reset vector:\n"
  "]FFFC 00 02\n"
  "Intel HEX format is allowed:\n"
  "]:02FFFC00000201\n"
  "Manually manipulating memory is useful for some light debugging, but the real\n"
  "power is from the other commands you can explore with this help system.\n"
  "Have fun!\n")

X(STR_HELP_DIR,
  "LS (also aliased as DIR) and CD are used to navigate USB mass storage\n"
  "devices. You can change to a different USB device with 0: to 7:. Use the\n"
  "STATUS command to get a list of mounted drives.\n")

X(STR_HELP_MKDIR,
  "MKDIR is used to create new directories. Use UNLINK to remove empty directories.\n")

X(STR_HELP_LOAD,
  "LOAD and INFO read ROM files from a USB drive. A ROM file contains both\n"
  "ASCII information for the user and binary information for the system.\n"
  "ROM file is an Atari XEX derived binary format. The file may contain\n"
  "many chunks loaded into different memory areas. The chunk marked for load\n"
  "into area starting from $FC00 is special and contains HELP/INFO information,\n"
  "which is not loaded into the RAM. It is used by the INFO command.\n"
  "If the ROM file contains data for the reset vector $FFFC-$FFFD then the\n"
  "CPU will be reset (started) immediately after loading.\n")

X(STR_HELP_INSTALL,
  "INSTALL and REMOVE manage the ROMs installed in the RP-RIA flash memory.\n"
  "ROM files must contain a reset vector to be installed. A list of installed\n"
  "ROMs is shown on the base HELP screen. Once installed, these ROMs become an\n"
  "integrated part of the system and can be loaded manually by simply using their\n"
  "name like any other command. The ROM name must not conflict with any other\n"
  "system command, must start with a letter, and may only contain up to 16 ASCII\n"
  "letters and numbers. If the file contains an extension, it must be \".xex\",\n"
  "which will be stripped upon install.\n")

X(STR_HELP_REBOOT,
  "REBOOT will restart the RP-RIA. It does the same thing as pressing a\n"
  "reboot button attached to the RIA RUN pin or interrupting the power supply.\n")

X(STR_HELP_RESET,
  "RESET will start the CPU by bringing RESB high. This monitor will be\n"
  "suspended until the CPU stops with a UART break, CTRL-ALT-DEL, or exit().\n")

X(STR_HELP_UPLOAD,
  "UPLOAD is used to send a file from another system to the local filesystem.\n"
  "The file may be any type with any name and will overwrite an existing file\n"
  "of the same name. For example, you can send a ROM file along with other\n"
  "files containing graphics or level data for a game. Then you can LOAD the\n"
  "game and test it. The upload is initiated with a filename.\n"
  "]UPLOAD filename.bin\n"
  "The system will respond with a \"}\" prompt or an error message starting with\n"
  "a \"?\". Any error will abort the upload and return you to the monitor.\n"
  "There is no retry as this is not intended to be used on lossy connections.\n"
  "Specify each chunk with a length, up to 1024 bytes, and CRC-32 which you can\n"
  "compute from any zip library.\n"
  "}$400 $0C0FFEE0\n"
  "Send the binary data and you will get another \"}\" prompt or \"?\" error.\n"
  "The transfer is completed with the END command or a blank line. Your choice.\n"
  "}END\n"
  "You will return to a \"]\" prompt on success or \"?\" error on failure.\n")

X(STR_HELP_UNLINK,
  "UNLINK removes a file. Its intended use is for scripting on another system\n"
  "connected to the monitor. For example, you might want to delete save data\n"
  "as part of automated testing.\n")

X(STR_HELP_BINARY,
  "BINARY is the fastest way to get code or data from your build system to the\n"
  "CPU RAM. Use the command \"BINARY addr len crc\" with a maximum length of 1024\n"
  "bytes and the CRC-32 calculated with a zip library. Then send the binary.\n"
  "You will return to a \"]\" prompt on success or \"?\" error on failure.\n")

X(STR_HELP_STATUS,
  "STATUS will show the status of all hardware in and connected to the RIA.\n")

X(STR_HELP_SET_PHI2,
  "PHI2 is the CPU clock speed in kHz. The valid range is " STR_PHI2_MIN_MAX " but not all\n"
  "frequencies are available. In that case, the next highest frequency will\n"
  "be automatically calculated and selected. Setting is saved on the RIA flash.\n")

X(STR_HELP_SET_BOOT,
  "BOOT selects an installed ROM to be automatically loaded and started when the\n"
  "system is powered up or rebooted. For example, you might want the system to\n"
  "immediately boot into BASIC or an operating system CLI. Using \"-\" for the\n"
  "argument will have the system boot into the monitor you are using now.\n"
  "Setting is saved on the RIA flash.\n")

X(STR_HELP_SET_TZ,
  "You can set your time zone from the table below with just the city like\n"
  "\"SET TZ LOS_ANGELES\" or the full name \"SET TZ AMERICA/LOS_ANGELES\".\n"
  "Locations not in the table below can SET TZ to a POSIX TZ string with, for\n"
  "example, \"SET TZ PST8PDT,M3.2.0/2,M11.1.0/2\". The easiest way to get a\n"
  "POSIX TZ string is to ask an AI \"posix tz for {your location}\".\n")

X(STR_HELP_SET_KB,
  "SET KB selects a keyboard layout. e.g. SET KB US\n")

X(STR_HELP_SET_CP,
  "SET CP selects a code page for system text. The following is supported: 437\n"
  "737, 771, 775, 850, 852, 855, 857, 860, 861, 862, 863, 864, 865, 866, 869.\n"
  "Code pages 720, 932, 936, 949, 950 are available but do not have VGA fonts.\n"
#if RP6502_CODE_PAGE
  "This is a development build. Only " STR_RP6502_CODE_PAGE " is available.\n"
#endif
  "")

X(STR_HELP_SET_RF,
  "SET RF (0|1) turns all radios off or on.\n")

X(STR_HELP_SET_RFCC,
  "Set this so the CYW43 can use the best radio frequencies for your country.\n"
  "Using \"-\" will clear the country code and default to a worldwide setting.\n")

X(STR_HELP_SET_SSID,
  "This is the Service Set Identifier for your WiFi network. Setting \"-\" will\n"
  "disable WiFi.\n")

X(STR_HELP_SET_PASS,
  "This is the password for your WiFi network. Use \"-\" to clear password.\n")

X(STR_HELP_SET_BLE,
  "Setting 0 disables Bluetooth LE. Setting 1 enables. Setting 2 enters pairing\n"
  "mode which will remain active until successful.\n")

#undef X

__in_flash("hlp_commands") static struct
{
    const char *const cmd;
    const char *const text;
    mon_response_fn extra_fn;
} const HLP_COMMANDS[] = {
    {STR_SET, STR_HELP_SET, NULL},
    {STR_STATUS, STR_HELP_STATUS, NULL},
    {STR_ABOUT, STR_HELP_ABOUT, NULL},
    {STR_CREDITS, STR_HELP_ABOUT, NULL},
    {STR_SYSTEM, STR_HELP_SYSTEM, NULL},
    {STR_0, STR_HELP_SYSTEM, NULL},
    {STR_0000, STR_HELP_SYSTEM, NULL},
    {STR_LS, STR_HELP_DIR, NULL},
    {STR_DIR, STR_HELP_DIR, NULL},
    {STR_CD, STR_HELP_DIR, NULL},
    {STR_CHDIR, STR_HELP_DIR, NULL},
    {STR_MKDIR, STR_HELP_MKDIR, NULL},
    {STR_0_COLON, STR_HELP_DIR, NULL},
    {STR_1_COLON, STR_HELP_DIR, NULL},
    {STR_2_COLON, STR_HELP_DIR, NULL},
    {STR_3_COLON, STR_HELP_DIR, NULL},
    {STR_4_COLON, STR_HELP_DIR, NULL},
    {STR_5_COLON, STR_HELP_DIR, NULL},
    {STR_6_COLON, STR_HELP_DIR, NULL},
    {STR_7_COLON, STR_HELP_DIR, NULL},
    {STR_8_COLON, STR_HELP_DIR, NULL},
    {STR_9_COLON, STR_HELP_DIR, NULL},
    {STR_LOAD, STR_HELP_LOAD, NULL},
    {STR_INFO, STR_HELP_LOAD, NULL},
    {STR_INSTALL, STR_HELP_INSTALL, NULL},
    {STR_REMOVE, STR_HELP_INSTALL, NULL},
    {STR_REBOOT, STR_HELP_REBOOT, NULL},
    {STR_RESET, STR_HELP_RESET, NULL},
    {STR_UPLOAD, STR_HELP_UPLOAD, NULL},
    {STR_UNLINK, STR_HELP_UNLINK, NULL},
    {STR_BINARY, STR_HELP_BINARY, NULL},
};
static const size_t COMMANDS_COUNT = sizeof HLP_COMMANDS / sizeof *HLP_COMMANDS;

__in_flash("hlp_settings") static struct
{
    const char *const cmd;
    const char *const text;
    mon_response_fn extra_fn;
} const HLP_SETTINGS[] = {
    {STR_PHI2, STR_HELP_SET_PHI2, NULL},
    {STR_BOOT, STR_HELP_SET_BOOT, NULL},
    {STR_TZ, STR_HELP_SET_TZ, clk_tzdata_response},
    {STR_KB, STR_HELP_SET_KB, kbd_layouts_response},
    {STR_CP, STR_HELP_SET_CP, NULL},
#ifdef RP6502_RIA_W
    {STR_RF, STR_HELP_SET_RF, NULL},
    {STR_RFCC, STR_HELP_SET_RFCC, cyw_country_code_response},
    {STR_SSID, STR_HELP_SET_SSID, NULL},
    {STR_PASS, STR_HELP_SET_PASS, NULL},
    {STR_BLE, STR_HELP_SET_BLE, NULL},
#endif
};
static const size_t SETTINGS_COUNT = sizeof HLP_SETTINGS / sizeof *HLP_SETTINGS;

static void help_response_lookup(const char *args, size_t len, const char **cp, mon_response_fn *fnp)
{
    *cp = NULL;
    *fnp = NULL;
    size_t cmd_len;
    for (cmd_len = 0; cmd_len < len; cmd_len++)
        if (args[cmd_len] == ' ')
            break;
    // SET command has another level of help
    if (cmd_len == strlen(STR_SET) && !strncasecmp(args, STR_SET, cmd_len))
    {
        args += cmd_len;
        len -= cmd_len;
        while (len && args[0] == ' ')
            args++, len--;
        size_t set_len;
        for (set_len = 0; set_len < len; set_len++)
            if (args[set_len] == ' ')
                break;
        if (!set_len)
        {
            *cp = STR_HELP_SET;
            return;
        }
        for (size_t i = 0; i < SETTINGS_COUNT; i++)
            if (set_len == strlen(HLP_SETTINGS[i].cmd))
                if (!strncasecmp(args, HLP_SETTINGS[i].cmd, set_len))
                {
                    *cp = HLP_SETTINGS[i].text;
                    *fnp = HLP_SETTINGS[i].extra_fn;
                    return;
                }
        return;
    }
    // Help for commands and a couple special words.
    for (size_t i = 1; i < COMMANDS_COUNT; i++)
        if (cmd_len == strlen(HLP_COMMANDS[i].cmd))
            if (!strncasecmp(args, HLP_COMMANDS[i].cmd, cmd_len))
            {
                *cp = HLP_COMMANDS[i].text;
                *fnp = HLP_COMMANDS[i].extra_fn;
                return;
            }
    return;
}

void hlp_mon_help(const char *args, size_t len)
{
    if (!len)
    {
        mon_add_response_str(STR_HELP_HELP);
        mon_add_response_fn(rom_installed_response);
        return;
    }
    while (len && args[len - 1] == ' ')
        len--;
    const char *c;
    mon_response_fn fn;
    help_response_lookup(args, len, &c, &fn);
    if (c != NULL)
        mon_add_response_str(c);
    if (fn != NULL)
        mon_add_response_fn(fn);
    if (c == NULL && fn == NULL)
        rom_mon_help(args, len);
}

bool hlp_topic_exists(const char *buf, size_t buflen)
{
    const char *c;
    mon_response_fn fn;
    help_response_lookup(buf, buflen, &c, &fn);
    return c != NULL || fn != NULL;
}
