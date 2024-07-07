/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * Based on: https://github.com/Wren6991/PicoDVI/blob/14c27eae/software/libhyperram/
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ram.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "main.h"
#include "pico/time.h"
#include "ram.pio.h"
#include <stdio.h>

#define CTRL_PIN_CS   0
#define CTRL_PIN_CK   1
#define CTRL_PIN_RWDS 2

static uint hyperram_write_prog_offset;
static uint hyperram_read_prog_offset;

#define RAM_DIE_NO 2

typedef enum
{
    HRAM_CMD_READ = 0b10100000000000000000000000000000u,
    HRAM_CMD_WRITE = 0b00100000000000000000000000000000u,
    HRAM_CMD_REGREAD = 0b11100000000000000000000000000000u,
    HRAM_CMD_REGWRITE = 0b01100000000000000000000000000000u,
} hyperram_cmd_flags;

static uint16_t ram_die_info[2][RAM_DIE_NO];

static void ram_read_die_info()
{
    ram_reg_read_blocking(0x0, 0, &ram_die_info[0][0]);
    uint16_t data = ram_die_info[0][0];
    // uint8_t *ptr = (uint8_t *)&data;
    // printf("!!! %02x", *ptr++);
    // printf(" %02x\n", *ptr++);
    ram_reg_read_blocking(0x0, 1, &ram_die_info[0][1]);
    ram_reg_read_blocking(0x1, 0, &ram_die_info[1][0]);
    ram_reg_read_blocking(0x1, 1, &ram_die_info[1][1]);
}

void ram_task(void)
{
    static int run = 0;
    if (run == 0)
    {
        printf("in RAM task\n");
        run = 1;
    }
    else if (run == 1)
    {
        if (to_ms_since_boot(get_absolute_time()) > 200)
        {
            run = 2;
        }
    }
    else if (run == 2)
    {
        printf("reading\n");
        static const uint16_t TS[10] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888};
        static uint8_t BF[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        ram_read_blocking(0, BF, 8);
        printf("   >: %02x %02x %02x %02x %02x %02x %02x %02x\n\r", BF[0], BF[1], BF[2], BF[3], BF[4], BF[5], BF[6], BF[7]);
        for (uint32_t addr = 0; addr < 0x100; addr++)
        {
            ram_write_blocking(addr, addr);
        }
        for (uint32_t addr = 0; addr < 0x200; addr += 32)
        {
            ram_read_blocking(addr, BF, 32);
            printf("%04x:", addr);
            for (size_t i = 0; i < 32; ++i)
            {
                printf(" %02x", BF[i]);
            }
            printf("\n\r");
        }
        printf("   0:");
        for (uint32_t addr = 0; addr < 32; addr += 2)
        {
            ram_read_blocking(addr, BF, 2);
            printf(".%02x %02x", BF[0], BF[1]);
        }
        printf("\n\r");

        run = 3;
    }
    else if (run == 3)
    {
        for (uint8_t die = 0; die < RAM_DIE_NO; die++)
        {
            // uint16_t ID0 = __builtin_bswap16(ram_die_info[0][die]);
            // uint16_t ID1 = __builtin_bswap16(ram_die_info[1][die]);
            uint16_t ID0 = ram_die_info[0][die];
            uint16_t ID1 = ram_die_info[1][die];
#ifndef NDEBUG
            printf("%04x %04x\n", ID0, ID1);
#endif
            printf("MEM%d: ", die);
            switch (ID1)
            {
            case 0x00:
            case 0xff:
                printf("Not Found\n");
                break;
            case 0x01: // HYPERRAM 2.0
            {
                printf("HYPERRAM 2.0");
                uint8_t die_address = ID0 >> 14;
                if (die_address == die)
                {
                    uint8_t row_address_bits = (ID0 >> 8) & 0b11111;
                    uint8_t column_address_bits = (ID0 >> 4) & 0b1111;
                    uint8_t manufacturer = ID0 & 0b1111;
                    printf(" %2dMb %s\n",
                           ((1u << (row_address_bits + 1 + column_address_bits + 1)) * 16) >> 20,
                           manufacturer == 0x01 ? "Infineon" : "[Unknown]");
                }
                else
                {
                    printf(" [die id mismatch: %d]\n", die_address);
                }
            }
            break;
            default:
                printf("Unknown (%04x %04x)\n", ID0, ID1);
            }
        }

        run = 4;
    }
}

void ram_init(void)
{
    // Adjustments for GPIO performance. Important!
    for (uint i = VPU_RAM_DATA_BASE; i < VPU_RAM_DATA_BASE + 8; ++i)
    {
        // Setting both pull bits enables bus-keep function
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        pio_gpio_init(VPU_RAM_PIO, i);
        hw_set_bits(&VPU_RAM_PIO->input_sync_bypass, 1u << i);
    }

    for (uint i = VPU_RAM_CTRL_BASE; i < VPU_RAM_CTRL_BASE + 3; ++i)
    {
        gpio_set_input_hysteresis_enabled(i, false);
        pio_gpio_init(VPU_RAM_PIO, i);
        hw_set_bits(&VPU_RAM_PIO->input_sync_bypass, 1u << i);
    }
    gpio_pull_down(VPU_RAM_CTRL_BASE + CTRL_PIN_RWDS);

    // All controls low except CSn
    pio_sm_set_pins_with_mask(VPU_RAM_PIO, VPU_RAM_CMD_SM,
                              (1u << CTRL_PIN_CS) << VPU_RAM_CTRL_BASE,
                              0x7u << VPU_RAM_CTRL_BASE);
    // All controls output except RWDS (DQs will sort themselves out later)
    pio_sm_set_pindirs_with_mask(VPU_RAM_PIO, VPU_RAM_CMD_SM,
                                 (1u << CTRL_PIN_CS | 1u << CTRL_PIN_CK) << VPU_RAM_CTRL_BASE,
                                 0x7u << VPU_RAM_CTRL_BASE);

    const float DIV = 1.f;

    hyperram_write_prog_offset = pio_add_program(VPU_RAM_PIO, &hyperram_write_program);
    pio_sm_config c = hyperram_write_program_get_default_config(hyperram_write_prog_offset);
    sm_config_set_clkdiv(&c, DIV);
    sm_config_set_out_pins(&c, VPU_RAM_DATA_BASE, 8);
    sm_config_set_in_pins(&c, VPU_RAM_DATA_BASE);
    sm_config_set_set_pins(&c, VPU_RAM_CTRL_BASE + CTRL_PIN_RWDS, 1);
    sm_config_set_sideset_pins(&c, VPU_RAM_CTRL_BASE);
    // Use shift-to-left (this means we write to memory in the wrong endianness,
    // but we hide this by requiring word-aligned addresses)
    sm_config_set_in_shift(&c, false, true, 16);
    sm_config_set_out_shift(&c, false, true, 32);
    pio_sm_init(VPU_RAM_PIO, VPU_RAM_CMD_SM, hyperram_write_prog_offset, &c);
    pio_sm_set_enabled(VPU_RAM_PIO, VPU_RAM_CMD_SM, true);

    hyperram_read_prog_offset = pio_add_program(VPU_RAM_PIO, &hyperram_read_program);
    c = hyperram_read_program_get_default_config(hyperram_read_prog_offset);
    sm_config_set_clkdiv(&c, DIV);
    sm_config_set_in_pins(&c, VPU_RAM_DATA_BASE);
    sm_config_set_in_shift(&c, false, true, 8);
    pio_sm_init(VPU_RAM_PIO, VPU_RAM_READ_SM, hyperram_read_prog_offset, &c);
    pio_sm_set_enabled(VPU_RAM_PIO, VPU_RAM_READ_SM, false);

    ram_read_die_info();
}

// 12-byte command packet to push to the PIO SM. After pushing the packet, can
// either push write data, or wait for read data.
typedef struct
{
    uint32_t len;
    uint32_t cmd0;
    uint32_t cmd1_dir_jmp;
} hyperram_cmd_t;

// HyperRAM command format from S27KL0641 datasheet:
// ------+------------------------------+------------------------------------+
// Bits  | Name                         | Description                        |
// ------+------------------------------+------------------------------------+
// 47    | R/W#                         | 1 for read, 0 for write            |
//       |                              |                                    |
// 46    | AS                           | 0 for memory address space, 1 for  |
//       |                              | register space (write only)        |
//       |                              |                                    |
// 45    | Burst                        | 0 for wrapped, 1 for linear        |
//       |                              |                                    |
// 44:16 | Row and upper column address | Address bits 31:3, irrelevant bits |
//       |                              | should be 0s                       |
//       |                              |                                    |
// 15:3  | Reserved                     | Set to 0                           |
//       |                              |                                    |
// 2:0   | Lower column address         | Address bits 2:0                   |
// ------+------------------------------+------------------------------------+

static inline void _hyperram_cmd_init(hyperram_cmd_t *cmd, hyperram_cmd_flags flags, uint32_t addr, uint len)
{
    // HyperBus uses halfword addresses, not byte addresses.
    // printf("> %08x: %x:%d\n", flags, addr, len);
    addr = addr >> 1;
    uint32_t addr_l = addr & 0x7u;
    uint32_t addr_h = addr >> 3;
    // First byte is always 0xff (to set DQs to output), then 24-bit length in same FIFO word.
    // Length is number of halfwords, minus one.
    cmd->len = (0xffu << 24) | ((len - 1) & ((1u << 24) - 1));
    cmd->cmd0 = flags | addr_h;
    cmd->cmd1_dir_jmp = addr_l << 16;
    // printf("> %08x: %x %x:%d => %x\n", flags, addr, addr_l, len, cmd->cmd0);
}

void __not_in_flash_func(ram_read_blocking)(uint32_t addr, uint8_t *dst, uint len)
{
    hyperram_cmd_t cmd;
    _hyperram_cmd_init(&cmd, HRAM_CMD_READ, addr, len);
    // printf("> %08x %08x\n", cmd.cmd0, cmd.cmd1_dir_jmp);
    cmd.cmd1_dir_jmp |= hyperram_write_prog_offset + hyperram_write_offset_read;

    int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_chan_config = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dma_chan_config, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_chan_config, false);
    channel_config_set_write_increment(&dma_chan_config, true);
    channel_config_set_dreq(&dma_chan_config, pio_get_dreq(VPU_RAM_PIO, VPU_RAM_READ_SM, false));
    dma_channel_configure(
        dma_chan,
        &dma_chan_config,
        dst,
        &VPU_RAM_PIO->rxf[VPU_RAM_READ_SM],
        len,
        true // Start immediately
    );

    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.len);
    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd0);
    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd1_dir_jmp);
    pio_sm_set_enabled(VPU_RAM_PIO, VPU_RAM_READ_SM, true);
    dma_channel_wait_for_finish_blocking(dma_chan);

    pio_sm_set_enabled(VPU_RAM_PIO, VPU_RAM_READ_SM, false);
    pio_sm_clear_fifos(VPU_RAM_PIO, VPU_RAM_READ_SM);
    pio_sm_exec(VPU_RAM_PIO, VPU_RAM_READ_SM, pio_encode_jmp(hyperram_read_prog_offset));

    pio_sm_exec(VPU_RAM_PIO, VPU_RAM_CMD_SM, pio_encode_jmp(hyperram_write_prog_offset));
    pio_sm_clear_fifos(VPU_RAM_PIO, VPU_RAM_CMD_SM);
    // printf("< %02x\n", *dst);

    dma_channel_unclaim(dma_chan);
}

void __not_in_flash_func(ram_write_blocking)(uint32_t addr, uint8_t data)
{
    int len = 1;
    hyperram_cmd_t cmd;
    _hyperram_cmd_init(&cmd, HRAM_CMD_WRITE, addr, 123);
    cmd.cmd1_dir_jmp |= (0xffu << 8) | (hyperram_write_prog_offset + hyperram_write_offset_write);
    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.len);
    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd0);
    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd1_dir_jmp);
    pio_sm_put_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM, (data << 24) | (addr & 0x1));
}

// Note these are *byte* addresses, so are off by a factor of 2 from those given in datasheet
enum
{
    HRAM_REG_ID0 = 0u << 12 | 0u << 1,
    HRAM_REG_ID1 = 0u << 12 | 1u << 1,
    HRAM_REG_CFG0 = 1u << 12 | 0u << 1,
    HRAM_REG_CFG1 = 1u << 12 | 1u << 1,
};

void __not_in_flash_func(ram_reg_read_blocking)(uint8_t addr, uint8_t die, uint16_t *dst)
{
    hyperram_cmd_t cmd;
    _hyperram_cmd_init(&cmd, HRAM_CMD_REGREAD, 0, 1);
    cmd.cmd0 |= die << (3 + 8 * 2);
    cmd.cmd1_dir_jmp = addr << 16;
    printf("> %08x %08x\n", cmd.cmd0, cmd.cmd1_dir_jmp);
    cmd.cmd1_dir_jmp |= hyperram_write_prog_offset + hyperram_write_offset_read;
    pio_sm_put(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.len);
    pio_sm_put(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd0);
    pio_sm_put(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd1_dir_jmp);

    // start READ SM
    pio_sm_set_enabled(VPU_RAM_PIO, VPU_RAM_READ_SM, true);

    // *dst = (uint16_t)pio_sm_get_blocking(VPU_RAM_PIO, VPU_RAM_CMD_SM);
    *(((uint8_t *)dst) + 1) = (uint8_t)pio_sm_get_blocking(VPU_RAM_PIO, VPU_RAM_READ_SM);
    *((uint8_t *)dst) = (uint8_t)pio_sm_get_blocking(VPU_RAM_PIO, VPU_RAM_READ_SM);

    // stop READ SM, drop FIFO contents, restart program
    pio_sm_set_enabled(VPU_RAM_PIO, VPU_RAM_READ_SM, false);
    pio_sm_clear_fifos(VPU_RAM_PIO, VPU_RAM_READ_SM);
    pio_sm_exec(VPU_RAM_PIO, VPU_RAM_READ_SM, pio_encode_jmp(hyperram_read_prog_offset));

    pio_sm_exec(VPU_RAM_PIO, VPU_RAM_CMD_SM, pio_encode_jmp(hyperram_write_prog_offset));
    pio_sm_clear_fifos(VPU_RAM_PIO, VPU_RAM_CMD_SM);

    printf("< %04x\n", *dst);
}

// We are using an awful hack here to reuse the data write loop for sending a
// command packet followed immediately by a halfword of data. No worries about
// efficiency, because generally you only write to the config register once.
void ram_cfgreg_write(uint16_t wdata)
{
    hyperram_cmd_t cmd;
    _hyperram_cmd_init(&cmd, HRAM_CMD_REGWRITE, HRAM_REG_CFG0, 0);
    // Make sure SM has bottomed out on TX empty, because we're about to mess
    // with its control flow
    uint32_t txstall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + VPU_RAM_CMD_SM);
    VPU_RAM_PIO->fdebug = txstall_mask;
    while (!(VPU_RAM_PIO->fdebug & txstall_mask))
        ;
    // Set DQs to output (note that this uses exec(), so asserts CSn as a result,
    // as it doesn't set delay/sideset bits)
    pio_sm_set_consecutive_pindirs(VPU_RAM_PIO, VPU_RAM_CMD_SM, VPU_RAM_DATA_BASE, 8, true);
    // Preload Y register for the data loop (number of halfwords - 1)
    pio_sm_exec(VPU_RAM_PIO, VPU_RAM_CMD_SM, pio_encode_set(pio_y, 3));
    // Note the difference between offset_write and offset_write_loop is whether
    // RWDS is asserted first (only true for write)
    pio_sm_exec(VPU_RAM_PIO, VPU_RAM_CMD_SM, pio_encode_jmp(hyperram_write_prog_offset + hyperram_write_offset_write_loop));
    pio_sm_put(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd0);
    pio_sm_put(VPU_RAM_PIO, VPU_RAM_CMD_SM, cmd.cmd1_dir_jmp | wdata);
}
