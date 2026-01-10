/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "mon/fil.h"
#include "mon/hlp.h"
#include "mon/mon.h"
#include "mon/ram.h"
#include "mon/rom.h"
#include "mon/set.h"
#include "mon/tst.h"
#include "net/cyw.h"
#include "mon/str.h"
#include "sys/com.h"
#include "sys/mem.h"
#include "sys/rln.h"
#include "sys/sys.h"
#include "sys/vpu.h"
#include <fatfs/ff.h>
#include <littlefs/lfs.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#if defined(DEBUG_RIA_MON) || defined(DEBUG_RIA_MON_MON)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

#define X(name, value) \
    static const char __in_flash(STRINGIFY(name)) name[] = value;

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
X(STR_MEMTEST, "MEMTEST")

X(STR_ERR_MONITOR_RESPONSE_OVERFLOW, "?Monitor response overflow\n")
X(STR_ERR_UNKNOWN_COMMAND, "?Unknown command\n")
X(STR_ERR_UNKNOWN_NUMBER, "?Unknown error %d\n")

// Monitor prompts
X(STR_MON_PROMPT_NEWLINE, "\30\33[0m\33[?25h\n]")
X(STR_MON_PROMPT, "\30\33[0m\33[?25h]")

// More
X(STR_MON_MORE_SHOW, "--More--")
X(STR_MON_MORE_ERASE, "\b\b\b\b\b\b\b\b        \b\b\b\b\b\b\b\b")

// Littlefs errors
X(STR_ERR_LFS_IO, "?IO error\n")                      // -5
X(STR_ERR_LFS_CORRUPT, "?Corrupted\n")                // -84
X(STR_ERR_LFS_NOENT, "?No directory entry\n")         // -2
X(STR_ERR_LFS_EXIST, "?Entry already exists\n")       // -17
X(STR_ERR_LFS_NOTDIR, "?Entry is not a directrory\n") // -20
X(STR_ERR_LFS_ISDIR, "?Entry is a directrory\n")      // -21
X(STR_ERR_LFS_NOTEMPTY, "?Directory is not empty\n")  // -39
X(STR_ERR_LFS_BADF, "?Bad file number\n")             // -9
X(STR_ERR_LFS_FBIG, "?File too large\n")              // -27
X(STR_ERR_LFS_INVAL, "?Invalid parameter\n")          // -22
X(STR_ERR_LFS_NOSPC, "?No space left on device\n")    // -28
X(STR_ERR_LFS_NOMEM, "?No more memory available\n")   // -12
X(STR_ERR_LFS_NOATTR, "?No data/attr available\n")    // -61
X(STR_ERR_LFS_NAMETOOLONG, "?File name too long\n")   // -36

// FatFs errors
X(STR_ERR_FATFS_DISK_ERR, "?Disk error\n")                     // 1
X(STR_ERR_FATFS_INT_ERR, "?Internal error\n")                  // 2
X(STR_ERR_FATFS_NOT_READY, "?Not ready\n")                     // 3
X(STR_ERR_FATFS_NO_FILE, "?No file\n")                         // 4
X(STR_ERR_FATFS_NO_PATH, "?No path\n")                         // 5
X(STR_ERR_FATFS_INVALID_NAME, "?Invalid name\n")               // 6
X(STR_ERR_FATFS_DENIED, "?Denied\n")                           // 7
X(STR_ERR_FATFS_EXIST, "?Exists\n")                            // 8
X(STR_ERR_FATFS_INVALID_OBJECT, "?Invalid object\n")           // 9
X(STR_ERR_FATFS_WRITE_PROTECTED, "?Write protected\n")         // 10
X(STR_ERR_FATFS_INVALID_DRIVE, "?Invalid drive\n")             // 11
X(STR_ERR_FATFS_NOT_ENABLED, "?Not enabled\n")                 // 12
X(STR_ERR_FATFS_NO_FILESYSTEM, "?No filesystem\n")             // 13
X(STR_ERR_FATFS_MKFS_ABORTED, "?Aborted\n")                    // 14
X(STR_ERR_FATFS_TIMEOUT, "?Timeout\n")                         // 15
X(STR_ERR_FATFS_LOCKED, "?Locked\n")                           // 16
X(STR_ERR_FATFS_NOT_ENOUGH_CORE, "?Not enough core\n")         // 17
X(STR_ERR_FATFS_TOO_MANY_OPEN_FILES, "?Too many open files\n") // 18
X(STR_ERR_FATFS_INVALID_PARAMETER, "?Invalid parameter\n")     // 19

#undef X

// Response limit must accommodate SET and STATUS commands
#define MON_RESPONSE_FN_COUNT 16
static mon_response_fn mon_response_fn_list[MON_RESPONSE_FN_COUNT];
static const char *mon_response_str[MON_RESPONSE_FN_COUNT];
static int mon_response_state[MON_RESPONSE_FN_COUNT] =
    {[0 ... MON_RESPONSE_FN_COUNT - 1] = -1};
static int mon_response_line;
static int mon_response_pos = -1;
static bool mon_needs_newline = true;
static bool mon_needs_prompt = true;
static bool mon_needs_read_line = false;
static bool mon_needs_break = false;
static enum {
    MON_MORE_OFF,
    MON_MORE_START,
    MON_MORE_FLUSH,
    MON_MORE_END,
    MON_MORE_C0,
    MON_MORE_ESC,
    MON_MORE_CSI,
    MON_MORE_SS3,
} mon_more_state;

typedef void (*mon_function)(const char *, size_t);
__in_flash("mon_commands") static struct
{
    const char *const cmd;
    mon_function func;
} const MON_COMMANDS[] = {
    {STR_HELP, hlp_mon_help},
    {STR_H, hlp_mon_help},
    {STR_QUESTION_MARK, hlp_mon_help},
    {STR_STATUS, sys_mon_status},
    {STR_SET, set_mon_set},
    {STR_LS, fil_mon_ls},
    {STR_DIR, fil_mon_ls},
    {STR_CD, fil_mon_chdir},
    {STR_CHDIR, fil_mon_chdir},
    {STR_MKDIR, fil_mon_mkdir},
    {STR_LOAD, rom_mon_load},
    {STR_INFO, rom_mon_info},
    {STR_INSTALL, rom_mon_install},
    {STR_REMOVE, rom_mon_remove},
    {STR_REBOOT, sys_mon_reboot},
    {STR_RESET, sys_mon_reset},
    {STR_UPLOAD, fil_mon_upload},
    {STR_UNLINK, fil_mon_unlink},
    {STR_BINARY, ram_mon_binary},
    {STR_MEMTEST, tst_mon_memtest},
};
static const size_t MON_COMMANDS_COUNT = sizeof MON_COMMANDS / sizeof *MON_COMMANDS;

// Returns NULL if not found. Advances buf to start of args.
static mon_function mon_command_lookup(const char **buf, size_t buflen)
{
    size_t i;
    for (i = 0; i < buflen; i++)
    {
        if ((*buf)[i] != ' ')
            break;
    }
    const char *cmd = (*buf) + i;
    bool is_maybe_addr = false;
    bool is_not_addr = false;
    for (; i < buflen; i++)
    {
        uint8_t ch = (*buf)[i];
        if (isxdigit(ch) || ch == '-')
            is_maybe_addr = true;
        else if (ch == ' ')
            break;
        else if (ch == ':')
        {
            is_maybe_addr = true;
            i++;
            break;
        }
        else
            is_not_addr = true;
    }
    size_t cmd_len = (*buf) + i - cmd;
    for (; i < buflen; i++)
    {
        if ((*buf)[i] != ' ')
            break;
    }
    // cd for chdir, 00cd for r/w address
    if (cmd_len == 2 && !strncasecmp(cmd, STR_CD, cmd_len))
        is_not_addr = true;
    // 0:-7: and USB0:-USB7:
    if (fil_drive_exists(cmd, cmd_len))
    {
        *buf = cmd;
        return fil_mon_chdrive;
    }
    // address command
    if (is_maybe_addr && !is_not_addr)
    {
        *buf = cmd;
        return ram_mon_address;
    }
    *buf += i;
    for (i = 0; i < MON_COMMANDS_COUNT; i++)
    {
        if (cmd_len == strlen(MON_COMMANDS[i].cmd))
            if (!strncasecmp(cmd, MON_COMMANDS[i].cmd, cmd_len))
                return MON_COMMANDS[i].func;
    }
    return NULL;
}

bool mon_command_exists(const char *buf, size_t buflen)
{
    return !!mon_command_lookup(&buf, buflen);
}

static void mon_enter(bool timeout, const char *buf, size_t length)
{
    (void)timeout;
    assert(!timeout);
    mon_needs_prompt = true;
    const char *args = buf;
    stdio_flush();
    mon_function func = mon_command_lookup(&args, length);
    if (func)
        return func(args, length - (args - buf));
    if (rom_load_installed(buf, length))
        return;
    // Supress error for empty lines
    for (const char *b = buf; b < args; b++)
        if (b[0] != ' ')
            return mon_add_response_str(STR_ERR_UNKNOWN_COMMAND);
}

static int mon_str_response(char *buf, size_t buf_size, int state)
{
    if (state < 0)
        return state;
    size_t i = 0;
    const char *str = mon_response_str[0];
    for (; i + 1 < buf_size; i++)
    {
        char c = str[state];
        buf[i] = c;
        if (!c)
            return -1;
        state++;
        buf_size--;
    }
    buf[i] = 0;
    return state;
}

static const char *mon_lfs_lookup(int result)
{
    switch (-result)
    {
    case LFS_ERR_IO: // -5
        return STR_ERR_LFS_IO;
    case LFS_ERR_CORRUPT: // -84
        return STR_ERR_LFS_CORRUPT;
    case LFS_ERR_NOENT: // -2
        return STR_ERR_LFS_NOENT;
    case LFS_ERR_EXIST: // -17
        return STR_ERR_LFS_EXIST;
    case LFS_ERR_NOTDIR: // -20
        return STR_ERR_LFS_NOTDIR;
    case LFS_ERR_ISDIR: // -21
        return STR_ERR_LFS_ISDIR;
    case LFS_ERR_NOTEMPTY: // -39
        return STR_ERR_LFS_NOTEMPTY;
    case LFS_ERR_BADF: // -9
        return STR_ERR_LFS_BADF;
    case LFS_ERR_FBIG: // -27
        return STR_ERR_LFS_FBIG;
    case LFS_ERR_INVAL: // -22
        return STR_ERR_LFS_INVAL;
    case LFS_ERR_NOSPC: // -28
        return STR_ERR_LFS_NOSPC;
    case LFS_ERR_NOMEM: // -12
        return STR_ERR_LFS_NOMEM;
    case LFS_ERR_NOATTR: // -61
        return STR_ERR_LFS_NOATTR;
    case LFS_ERR_NAMETOOLONG: // -36
        return STR_ERR_LFS_NAMETOOLONG;
    default:
        return NULL;
    }
}

static int mon_lfs_response(char *buf, size_t buf_size, int state)
{
    if (state < 0)
        return state;
    const char *err_str = mon_lfs_lookup(state);
    if (err_str != NULL)
        snprintf(buf, buf_size, err_str);
    else
        snprintf(buf, buf_size, STR_ERR_UNKNOWN_NUMBER, state);
    return -1;
}

static const char *mon_fatfs_lookup(int fresult)
{
    switch (fresult)
    {
    case FR_DISK_ERR: // 1
        return STR_ERR_FATFS_DISK_ERR;
    case FR_INT_ERR: // 2
        return STR_ERR_FATFS_INT_ERR;
    case FR_NOT_READY: // 3
        return STR_ERR_FATFS_NOT_READY;
    case FR_NO_FILE: // 4
        return STR_ERR_FATFS_NO_FILE;
    case FR_NO_PATH: // 5
        return STR_ERR_FATFS_NO_PATH;
    case FR_INVALID_NAME: // 6
        return STR_ERR_FATFS_INVALID_NAME;
    case FR_DENIED: // 7
        return STR_ERR_FATFS_DENIED;
    case FR_EXIST: // 8
        return STR_ERR_FATFS_EXIST;
    case FR_INVALID_OBJECT: // 9
        return STR_ERR_FATFS_INVALID_OBJECT;
    case FR_WRITE_PROTECTED: // 10
        return STR_ERR_FATFS_WRITE_PROTECTED;
    case FR_INVALID_DRIVE: // 11
        return STR_ERR_FATFS_INVALID_DRIVE;
    case FR_NOT_ENABLED: // 12
        return STR_ERR_FATFS_NOT_ENABLED;
    case FR_NO_FILESYSTEM: // 13
        return STR_ERR_FATFS_NO_FILESYSTEM;
    case FR_MKFS_ABORTED: // 14
        return STR_ERR_FATFS_MKFS_ABORTED;
    case FR_TIMEOUT: // 15
        return STR_ERR_FATFS_TIMEOUT;
    case FR_LOCKED: // 16
        return STR_ERR_FATFS_LOCKED;
    case FR_NOT_ENOUGH_CORE: // 17
        return STR_ERR_FATFS_NOT_ENOUGH_CORE;
    case FR_TOO_MANY_OPEN_FILES: // 18
        return STR_ERR_FATFS_TOO_MANY_OPEN_FILES;
    case FR_INVALID_PARAMETER: // 19
        return STR_ERR_FATFS_INVALID_PARAMETER;
    default:
        return NULL;
    }
}

static int mon_fatfs_response(char *buf, size_t buf_size, int state)
{
    if (state < 0)
        return state;
    const char *err_str = mon_fatfs_lookup(state);
    if (err_str != NULL)
        snprintf(buf, buf_size, err_str);
    else
        snprintf(buf, buf_size, STR_ERR_UNKNOWN_NUMBER, state);
    return -1;
}

static void mon_append_response(mon_response_fn fn, const char *str, int state)
{
    assert(state >= 0);
    int i = 0;
    for (; i < MON_RESPONSE_FN_COUNT; i++)
    {
        if (!mon_response_fn_list[i])
        {
            mon_response_fn_list[i] = fn;
            mon_response_str[i] = str;
            mon_response_state[i] = state;
            return;
        }
    }
    if (i == MON_RESPONSE_FN_COUNT)
    {
        i--;
        mon_response_fn_list[i] = mon_str_response;
        mon_response_str[i] = STR_ERR_MONITOR_RESPONSE_OVERFLOW;
        mon_response_state[i] = 0;
    }
}

static void mon_next_response(void)
{
    int i = 0;
    for (; i < MON_RESPONSE_FN_COUNT - 1; i++)
    {
        mon_response_fn_list[i] = mon_response_fn_list[i + 1];
        mon_response_str[i] = mon_response_str[i + 1];
        mon_response_state[i] = mon_response_state[i + 1];
    }
    mon_response_fn_list[i] = NULL;
    mon_response_str[i] = NULL;
    mon_response_state[i] = -1;
}

static void mon_break_response(void)
{
    mon_needs_break = false;
    mon_response_pos = -1;
    for (int i = 0; i < MON_RESPONSE_FN_COUNT; i++)
    {
        if (mon_response_state[i] >= 0)
        {
            mon_response_fn_list[i](response_buf, RESPONSE_BUF_SIZE, -1);
            mon_response_fn_list[i] = NULL;
            mon_response_str[i] = NULL;
            mon_response_state[i] = -1;
        }
    }
}

void mon_add_response_fn(mon_response_fn fn)
{
    mon_append_response(fn, NULL, 0);
}

void mon_add_response_fn_state(mon_response_fn fn, int state)
{
    mon_append_response(fn, NULL, state);
}

void mon_add_response_str(const char *str)
{
    mon_append_response(mon_str_response, str, 0);
}

void mon_add_response_lfs(int result)
{
    if (result < 0)
        mon_append_response(mon_lfs_response, NULL, -result);
}

void mon_add_response_fatfs(int fresult)
{
    if (fresult > 0)
        mon_append_response(mon_fatfs_response, NULL, fresult);
}

static void mon_more(void)
{
    if (mon_needs_break)
    {
        mon_needs_newline = false;
        if (mon_more_state == MON_MORE_START)
            return;
        mon_more_state = MON_MORE_END;
    }
    switch (mon_more_state)
    {
    case MON_MORE_START:
        printf(STR_MON_MORE_SHOW);
        mon_more_state = MON_MORE_FLUSH;
        break;
    case MON_MORE_FLUSH:
        if (PICO_ERROR_TIMEOUT == stdio_getchar_timeout_us(0))
            mon_more_state = MON_MORE_C0;
        break;
    case MON_MORE_END:
        printf(STR_MON_MORE_ERASE);
        mon_response_line = 0;
        mon_more_state = MON_MORE_OFF;
        break;
    default:
        int ch = stdio_getchar_timeout_us(0);
        if (ch == '\30')
            mon_more_state = MON_MORE_C0;
        else if (ch != PICO_ERROR_TIMEOUT)
            switch (mon_more_state)
            {
            default: // MON_MORE_C0
                if (ch == '\33')
                    mon_more_state = MON_MORE_ESC;
                else
                    mon_more_state = MON_MORE_END;
                if (ch == 3 || ch == 'q' || ch == 'Q')
                    mon_needs_break = true;
                break;
            case MON_MORE_ESC:
                if (ch == '[')
                    mon_more_state = MON_MORE_CSI;
                else if (ch == 'O')
                    mon_more_state = MON_MORE_SS3;
                else
                    mon_more_state = MON_MORE_END;
                break;
            case MON_MORE_CSI:
                if (ch < 0x20 || ch > 0x3F)
                    mon_more_state = MON_MORE_END;
                break;
            case MON_MORE_SS3:
                mon_more_state = MON_MORE_END;
                break;
            }
    }
}

static int mon_guess_console_rows(void)
{
    int rows = 24; // VT100 safe
    rows = 30;
    return rows;
}

void mon_task(void)
{
    // The monitor must never print while 6502 is running.
    if (main_active())
        return;
    if (mon_more_state)
        return mon_more();
    if (mon_needs_break)
        return mon_break_response();
    // Flush the current response buffer
    if (mon_response_pos >= 0)
    {
        int rows_max = mon_guess_console_rows() - 1;
        char c;
        while ((c = response_buf[mon_response_pos]) && com_putchar_ready())
        {
            if (mon_response_line >= rows_max)
            {
                mon_more_state = MON_MORE_START;
                break;
            }
            putchar(c);
            mon_response_pos++;
            if (c == '\n')
                mon_response_line++;
        }
        if (!c)
            mon_response_pos = -1;
        return;
    }
    // Request the next response buffer
    if (mon_response_pos == -1 && mon_response_state[0] >= 0)
    {
        mon_response_pos = 0;
        response_buf[0] = 0;
        mon_response_state[0] = (mon_response_fn_list[0])(
            response_buf, RESPONSE_BUF_SIZE, mon_response_state[0]);
        if (mon_response_state[0] < 0)
            mon_next_response();
        return;
    }
    // These can run the 6502 multiple times
    if (ram_active() ||
        rom_active() ||
        fil_active())
        return;
    // The monitor has control
    if (mon_needs_prompt)
    {
        if (mon_needs_newline)
            mon_add_response_str(STR_MON_PROMPT_NEWLINE);
        else
            mon_add_response_str(STR_MON_PROMPT);
        mon_needs_prompt = false;
        mon_needs_newline = false;
        mon_needs_read_line = true;
        mon_response_line = 0;
        return;
    }
    if (mon_needs_read_line)
    {
        mon_needs_read_line = false;
        mon_response_line = 0;
        rln_read_line(0, mon_enter, 256, 0);
        return;
    }
}

void mon_break(void)
{
    mon_needs_prompt = true;
    mon_needs_newline = true;
    mon_needs_break = true;
}
