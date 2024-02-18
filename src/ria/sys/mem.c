/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/gpio.h"
#include "main.h"
#include <stdio.h>

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

void mem_init(void)
{
}

void mem_task(void)
{
}

static const uint8_t READ_ID = 0x9f;

static void mem_read_id(int8_t bank, uint8_t ID[8])
{
    const uint8_t COMMAND[4] = {
        READ_ID
        // next 24 bit address, unused by command
    };

    // TODO: implement
    (void)COMMAND;
    (void)bank;
    (void)ID;
}

void mem_print_status(void)
{
    for (uint8_t i = 0; i < MEM_BANKS; i++)
    {
        uint8_t ID[8] = {0};
        mem_read_id(i, ID);
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
}
