/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "main.h"
#include <stdio.h>

#ifdef NDEBUG
uint8_t xram[0x10000];
#else
static struct
{
    uint8_t _0[0x1000];
    uint8_t _1[0x1000];
    uint8_t _2[0x1000];
    uint8_t _3[0x1000];
    uint8_t _4[0x1000];
    uint8_t _5[0x1000];
    uint8_t _6[0x1000];
    uint8_t _7[0x1000];
    uint8_t _8[0x1000];
    uint8_t _9[0x1000];
    uint8_t _A[0x1000];
    uint8_t _B[0x1000];
    uint8_t _C[0x1000];
    uint8_t _D[0x1000];
    uint8_t _E[0x1000];
    uint8_t _F[0x1000];
    // this struct of 4KB segments is because
    // a single 64KB array crashes my debugger
} xram_blocks;
uint8_t *const xram = (uint8_t *)&xram_blocks;
#endif

uint8_t xstack[XSTACK_SIZE + 1];
size_t volatile xstack_ptr;

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

void mem_init(void)
{
    spi_init(MEM_SPI, MEM_SPI_BAUDRATE);
    gpio_set_function(MEM_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MEM_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MEM_SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(MEM_SPI_CS0_PIN);
    gpio_set_dir(MEM_SPI_CS0_PIN, GPIO_OUT);
    gpio_init(MEM_SPI_CS1_PIN);
    gpio_set_dir(MEM_SPI_CS1_PIN, GPIO_OUT);
    gpio_init(MEM_SPI_CS2_PIN);
    gpio_set_dir(MEM_SPI_CS2_PIN, GPIO_OUT);
    gpio_init(MEM_SPI_CS3_PIN);
    gpio_set_dir(MEM_SPI_CS3_PIN, GPIO_OUT);
    mem_select_bank(-1);
}

void mem_task(void)
{
}

void mem_select_bank(int8_t bank)
{
    // asm volatile("nop \n nop \n nop");
    gpio_put(MEM_SPI_CS0_PIN, bank != 0);
    gpio_put(MEM_SPI_CS1_PIN, bank != 1);
    gpio_put(MEM_SPI_CS2_PIN, bank != 2);
    gpio_put(MEM_SPI_CS3_PIN, bank != 3);
    // asm volatile("nop \n nop \n nop");
}

static const uint8_t READ_ID = 0x9f;

static void mem_read_id(uint8_t ID[8])
{
    // Read ID command
    spi_write_blocking(MEM_SPI, &READ_ID, 1);
    // next 24 bit address, unused by command
    const uint8_t ADDRESS[3] = {0};
    spi_write_blocking(MEM_SPI, ADDRESS, 3);

    spi_read_blocking(MEM_SPI, 0, ID, 8);
}

void mem_print_status(void)
{
    for (uint8_t i = 0; i < MEM_SPI_BANKS; i++)
    {
        uint8_t ID[8] = {0};
        mem_select_bank(i);
        mem_read_id(ID);
        // #ifndef NDEBUG
        //         printf("%llx\n", *((uint64_t *)ID));
        // #endif
        printf("MEM%d: ", i);
        switch (ID[0])
        {
        case 0:
            printf("Not Found\n");
            break;
        case 0x9d: // ISSI
        {
            uint8_t device_density = (ID[2] & 0b11100000) >> 5;
            printf("%dMb ISSI%s\n",
                   device_density == 0b000   ? 8
                   : device_density == 0b001 ? 16
                   : device_density == 0b010 ? 32
                                             : 0,
                   ID[1] != 0x5d ? " Not Passed" : 0);
        }
        break;
        default:
            printf("Unknown (0x%02x)\n", ID[0]);
        }
    }
    mem_select_bank(-1);
}
