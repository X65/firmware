/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fatfs/ff.h"
#include "main.h"
#include "mon/hlp.h"
#include "mon/mon.h"
#include "str.h"
#include "sys/cfg.h"
#include "sys/lfs.h"
#include "sys/mem.h"

static enum {
    ROM_IDLE,
    ROM_LOADING,
    ROM_WRITING,
} rom_state;
static uint8_t rom_bank;
static uint16_t rom_start;
static uint16_t rom_end;
static bool rom_FFFC;
static bool rom_FFFD;
static bool is_reading_fat;
static bool lfs_file_open;
static lfs_file_t lfs_file;
static LFS_FILE_CONFIG(lfs_file_config) static FIL fat_fil;

static bool rom_eof(void)
{
    if (is_reading_fat)
        return !!f_eof(&fat_fil);
    else
        return !!lfs_eof(&lfs_file);
}

static bool rom_read(uint32_t len)
{
    if (is_reading_fat)
    {
        FRESULT result = f_read(&fat_fil, mbuf, len, &mbuf_len);
        if (result != FR_OK)
        {
            printf("?Unable to read file (%d)\n", result);
            return false;
        }
    }
    else
    {
        lfs_ssize_t lfsresult = lfs_file_read(&lfs_volume, &lfs_file, mbuf, len);
        if (lfsresult < 0)
        {
            printf("?Unable to lfs_file_read (%ld)\n", lfsresult);
            return false;
        }
        mbuf_len = lfsresult;
    }
    if (len != mbuf_len)
    {
        printf("?Unable to read binary data\n");
        return false;
    }
    return true;
}

static bool rom_open(const char *name, bool is_fat)
{
    is_reading_fat = is_fat;
    if (is_fat)
    {
        FRESULT result = f_open(&fat_fil, name, FA_READ);
        if (result != FR_OK)
        {
            printf("?Unable to open file (%d)\n", result);
            return false;
        }
    }
    else
    {
        int lfsresult = lfs_file_opencfg(&lfs_volume, &lfs_file, name,
                                         LFS_O_RDONLY, &lfs_file_config);
        if (lfsresult < 0)
        {
            printf("?Unable to lfs_file_opencfg (%d)\n", lfsresult);
            return false;
        }
        lfs_file_open = true;
    }
    if (!rom_read(2) || mbuf[0] != 0xFF || mbuf[1] != 0xFF)
    {
        printf("?Missing XEX header in ROM file\n");
        rom_state = ROM_IDLE;
        return false;
    }
    rom_FFFC = false;
    rom_FFFD = false;
    rom_start = 0xFFFF;
    rom_bank = 0;
    return true;
}

static bool rom_next_chunk(void)
{
    if (rom_start == 0xFFFF || rom_start >= rom_end)
    {
        // read header
        if (!rom_read(2))
        {
            printf("?Missing XEX block header\n");
            return false;
        }
        if (mbuf[0] == 0xFF && mbuf[1] == 0xFF)
        {
            // optional block header marker - try next one
            if (!rom_read(2))
            {
                printf("?Missing XEX block header\n");
                return false;
            }
        }
        rom_start = mbuf[0] | (mbuf[1] << 8);
        if (!rom_read(2))
        {
            printf("?Corrupt XEX block header\n");
            return false;
        }
        rom_end = mbuf[0] | (mbuf[1] << 8);
        if (rom_end < rom_start)
        {
            printf("?Invalid XEX block header\n");
            return false;
        }
        // printf("Loading XEX block: $%04X - $%04X\n", rom_start, rom_end);
    }

    uint16_t rom_len = rom_end - rom_start + 1;
    return rom_read(MIN(rom_len, MBUF_SIZE));
}

static void rom_loading(void)
{
    if (rom_eof())
    {
        rom_state = ROM_IDLE;
        if (rom_FFFC && rom_FFFD)
            main_run();
        else
            printf("Loaded. No reset vector.\n");
        return;
    }
    if (!rom_next_chunk())
    {
        rom_state = ROM_IDLE;
        return;
    }
    if (mbuf_len)
    {
        rom_state = ROM_WRITING;
    }
}

static bool rom_ram_writing(bool test)
{
    if (rom_start == rom_end && rom_start == 0xFFFE)
    {
        rom_bank = mbuf[0];
        return false;
    }

    uint16_t i = 0;
    while (mbuf_len--)
    {
        uint32_t addr = (rom_bank << 16) | rom_start++;
        if (addr == 0xFFFC)
            rom_FFFC = true;
        if (addr == 0xFFFD)
            rom_FFFD = true;

        if (!test)
        {
            uint8_t data = mbuf[i++];
            if (addr >= 0xFFE4 && addr <= 0xFFFF)
            {
                switch (addr)
                {
                case 0xFFE4: // COP_L
                case 0xFFE5: // COP_H
                case 0xFFE6: // BRK_L
                case 0xFFE7: // BRK_H
                case 0xFFE8: // ABORTB_L
                case 0xFFE9: // ABORTB_H
                case 0xFFEA: // NMIB_L
                case 0xFFEB: // NMIB_H
                case 0xFFEE: // IRQB_L
                case 0xFFEF: // IRQB_H
                case 0xFFF4: // COP_L
                case 0xFFF5: // COP_H
                case 0xFFF8: // ABORTB_L
                case 0xFFF9: // ABORTB_H
                case 0xFFFA: // NMIB_L
                case 0xFFFB: // NMIB_H
                case 0xFFFC: // RESETB_l
                case 0xFFFD: // RESETB_H
                case 0xFFFE: // IRQB/BRK_L
                case 0xFFFF: // IRQB/BRK_H
                    REGS(addr) = data;
                }
            }
            psram[addr] = data;
        }
    }
    return (int)mbuf_len > 0;
}

void rom_mon_install(const char *args, size_t len)
{
    // Strip special extension, validate and upcase name
    char lfs_name[LFS_NAME_MAX + 1];
    size_t lfs_name_len = len;
    while (lfs_name_len && args[lfs_name_len - 1] == ' ')
        lfs_name_len--;
    if (lfs_name_len > 4)
        if (!strnicmp(".xex", args + lfs_name_len - 4, 4))
            lfs_name_len -= 4;
    if (lfs_name_len > LFS_NAME_MAX)
        lfs_name_len = 0;
    lfs_name[lfs_name_len] = 0;
    memcpy(lfs_name, args, lfs_name_len);
    for (size_t i = 0; i < lfs_name_len; i++)
    {
        if (lfs_name[i] >= 'a' && lfs_name[i] <= 'z')
            lfs_name[i] -= 32;
        if (lfs_name[i] >= 'A' && lfs_name[i] <= 'Z')
            continue;
        if (i && lfs_name[i] >= '0' && lfs_name[i] <= '9')
            continue;
        lfs_name_len = 0;
    }
    // Test for system conflicts
    if (!lfs_name_len
        || mon_command_exists(lfs_name, lfs_name_len)
        || help_text_lookup(lfs_name, lfs_name_len))
    {
        printf("?Invalid ROM name.\n");
        return;
    }

    // Test contents of file
    if (!rom_open(args, true))
        return;
    while (!rom_eof())
        if (rom_next_chunk())
        {
            rom_ram_writing(true);
        }
        else
            return;
    if (!rom_FFFC || !rom_FFFD)
    {
        printf("?No reset vector.\n");
        return;
    }
    FRESULT fresult = f_rewind(&fat_fil);
    if (fresult != FR_OK)
    {
        printf("?Unable to rewind file (%d)\n", fresult);
        return;
    }
    int lfsresult = lfs_file_opencfg(&lfs_volume, &lfs_file, lfs_name,
                                     LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL,
                                     &lfs_file_config);
    if (lfsresult < 0)
    {
        if (lfsresult == LFS_ERR_EXIST)
            printf("?ROM already exists.\n");
        else
            printf("?Unable to lfs_file_opencfg (%d)\n", lfsresult);
        return;
    }
    lfs_file_open = true;
    while (true)
    {
        fresult = f_read(&fat_fil, mbuf, MBUF_SIZE, &mbuf_len);
        if (fresult != FR_OK)
        {
            printf("?Unable to read file (%d)\n", fresult);
            break;
        }
        lfsresult = lfs_file_write(&lfs_volume, &lfs_file, mbuf, mbuf_len);
        if (lfsresult < 0)
        {
            printf("?Unable to lfs_file_write (%d)\n", lfsresult);
            break;
        }
        if (mbuf_len < MBUF_SIZE)
            break;
    }
    int lfscloseresult = lfs_file_close(&lfs_volume, &lfs_file);
    lfs_file_open = false;
    if (lfscloseresult < 0)
        printf("?Unable to lfs_file_close (%d)\n", lfscloseresult);
    if (lfsresult >= 0)
        lfsresult = lfscloseresult;
    fresult = f_close(&fat_fil);
    if (fresult != FR_OK)
        printf("?Unable to f_close file (%d)\n", fresult);
    if (fresult == FR_OK && lfsresult >= 0)
        printf("Installed %s.\n", lfs_name);
    else
        lfs_remove(&lfs_volume, lfs_name);
}

void rom_mon_remove(const char *args, size_t len)
{
    char lfs_name[LFS_NAME_MAX + 1];
    if (parse_rom_name(&args, &len, lfs_name)
        && parse_end(args, len))
    {
        const char *boot = cfg_get_boot();
        if (!strcmp(lfs_name, boot))
        {
            printf("?Unable to remove boot ROM\n");
            return;
        }
        int lfsresult = lfs_remove(&lfs_volume, lfs_name);
        if (lfsresult < 0)

            printf("?Unable to lfs_remove (%d)\n", lfsresult);
        else
            printf("Removed %s.\n", lfs_name);
        return;
    }
    printf("?Invalid ROM name\n");
}

void rom_mon_load(const char *args, size_t len)
{
    (void)(len);
    if (rom_open(args, true))
        rom_state = ROM_LOADING;
}

bool rom_load(const char *args, size_t len)
{
    char lfs_name[LFS_NAME_MAX + 1];
    if (parse_rom_name(&args, &len, lfs_name)
        && parse_end(args, len))
    {
        struct lfs_info info;
        if (lfs_stat(&lfs_volume, lfs_name, &info) < 0)
            return false;
        if (rom_open(lfs_name, false))
            rom_state = ROM_LOADING;
        return true;
    }
    return false;
}

void rom_mon_info(const char *args, size_t len)
{
    (void)(len);
    if (!rom_open(args, true))
        return;
    bool found = false;
    while (!rom_eof() && rom_next_chunk())
    {
        if (rom_start == 0xFC00)
            found = true;
        if (found)
            printf("%.*s", mbuf_len, mbuf);
        rom_start += mbuf_len;
        if (found && rom_start >= rom_end)
        {
            putc('\n', stdout);
            break;
        }
    }
    if (!found)
        puts("?No help found in file.");
}

// Returns false and prints nothing if ROM not found.
// Something will always print before returning true.
bool rom_help(const char *args, size_t len)
{
    char lfs_name[LFS_NAME_MAX + 1];
    if (parse_rom_name(&args, &len, lfs_name)
        && parse_end(args, len))
    {
        struct lfs_info info;
        if (lfs_stat(&lfs_volume, lfs_name, &info) < 0)
            return false;
        bool found = false;
        if (rom_open(lfs_name, false))
            while (!rom_eof() && rom_next_chunk())
            {
                if (rom_start == 0xFC00)
                    found = true;
                if (found)
                    printf("%.*s", mbuf_len, mbuf);
                rom_start += mbuf_len;
                if (found && rom_start >= rom_end)
                {
                    putc('\n', stdout);
                    break;
                }
            }
        if (!found)
            puts("?No help found in ROM.");
        return true; // even when !found
    }
    return false;
}

void rom_init(void)
{
    // Try booting the set boot ROM
    char *boot = cfg_get_boot();
    size_t boot_len = strlen(boot);
    rom_load((char *)boot, boot_len);
}

void rom_task(void)
{
    switch (rom_state)
    {
    case ROM_IDLE:
        break;
    case ROM_LOADING:
        rom_loading();
        break;
    case ROM_WRITING:
        if (!rom_ram_writing(false))
            rom_state = ROM_LOADING;
        break;
    }

    if (rom_state == ROM_IDLE && lfs_file_open)
    {
        int lfsresult = lfs_file_close(&lfs_volume, &lfs_file);
        lfs_file_open = false;
        if (lfsresult < 0)
            printf("?Unable to lfs_file_close (%d)\n", lfsresult);
    }

    if (rom_state == ROM_IDLE && fat_fil.obj.fs)
    {
        FRESULT result = f_close(&fat_fil);
        if (result != FR_OK)
            printf("?Unable to close file (%d)\n", result);
    }
}

bool rom_active(void)
{
    return rom_state != ROM_IDLE;
}

void rom_reset(void)
{
    rom_state = ROM_IDLE;
}
