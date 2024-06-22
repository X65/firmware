#include "hw.h"

#include "mem.h"

uint8_t __attribute__((optimize("O1")))
hw_read(uint8_t address)
{
    switch ((address >> 5) & 0x3)
    {
    case 0b11: // RIA
    {
        return REGS(address);
    }
    default:
    {
        return 0xEA; // give NOP
    }
    }
}

void __attribute__((optimize("O1")))
hw_write(uint8_t address, uint8_t byte)
{
    switch ((address >> 5) & 0x3)
    {
    case 0b11: // RIA
    {
        REGS(address) = byte;
        break;
    }
    default:
    {
        (void)byte;
    }
    }
}
