/*
 * Copyright (c) 2026 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/cia.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "sys/bus.h"

// A timer counts down once per microsecond. It underflows one tick after
// reaching 0 and reloads its latch, so a period is latch + 1 microseconds.
// Timer B can count Timer A underflows instead. The CNT input is not
// connected, so CNT counting modes never count, like in the X65 emulator.

#define CIA_REG_TALO 0
#define CIA_REG_TAHI 1
#define CIA_REG_TBLO 2
#define CIA_REG_TBHI 3
#define CIA_REG_ICR  5
#define CIA_REG_CRA  6
#define CIA_REG_CRB  7

#define CIA_CR_START      0b00000001
#define CIA_CR_ONESHOT    0b00001000
#define CIA_CR_FORCE_LOAD 0b00010000
#define CIA_CRA_INMODE    0b00100000 // 0: PHI2, 1: CNT
#define CIA_CRB_INMODE    0b01100000 // 00: PHI2, 01: CNT, 10: TA, 11: TA & CNT
#define CIA_CRB_INMODE_TA 0b01000000

#define CIA_ICR_TA  0b00000001
#define CIA_ICR_TB  0b00000010
#define CIA_ICR_IR  0b10000000
#define CIA_IMR_BITS 0b00011111

// Underflows closer than this are handled in batches, one alarm
// interrupt per batch, so a tiny latch can't starve the bus ISR.
#define CIA_MIN_ALARM_US 10

typedef struct
{
    uint16_t latch;
    uint16_t counter; // valid while not counting microseconds
    uint8_t cr;
    bool timed;      // counting microseconds, underflows at target
    uint64_t target; // time_us_64() of the next underflow
    int alarm;
} cia_timer_t;

static cia_timer_t cia_ta;
static cia_timer_t cia_tb;
static uint8_t cia_icr;
static uint8_t cia_imr;

bool cia_irq(void)
{
    return cia_icr & CIA_ICR_IR;
}

static void cia_update_icr(void)
{
    if (cia_icr & cia_imr)
        cia_icr |= CIA_ICR_IR;
    bus_set_cia_irq(cia_irq());
}

static bool cia_counts_us(const cia_timer_t *t)
{
    if (!(t->cr & CIA_CR_START))
        return false;
    if (t == &cia_ta)
        return !(t->cr & CIA_CRA_INMODE);
    return !(t->cr & CIA_CRB_INMODE);
}

static uint16_t cia_count(const cia_timer_t *t)
{
    if (!t->timed)
        return t->counter;
    const uint64_t now = time_us_64();
    return t->target > now ? (uint16_t)(t->target - now - 1) : 0;
}

static void cia_underflow(cia_timer_t *t);

// Timer B counting Timer A underflows
static void cia_tb_count_ta(void)
{
    if (!(cia_tb.cr & CIA_CR_START) || !(cia_tb.cr & CIA_CRB_INMODE_TA))
        return;
    if (--cia_tb.counter == 0)
        cia_underflow(&cia_tb);
}

static void cia_underflow(cia_timer_t *t)
{
    cia_icr |= (t == &cia_ta) ? CIA_ICR_TA : CIA_ICR_TB;
    cia_update_icr();
    t->counter = t->latch;
    if (t->cr & CIA_CR_ONESHOT)
    {
        t->cr &= ~CIA_CR_START;
        t->timed = false;
    }
    else if (t->timed)
        t->target += (uint32_t)t->latch + 1;
    if (t == &cia_ta)
        cia_tb_count_ta();
}

// Handle every underflow that is due, then set the alarm for the next one.
static void cia_schedule(cia_timer_t *t)
{
    while (t->timed)
    {
        const uint64_t now = time_us_64();
        if (t->target <= now)
        {
            cia_underflow(t);
            continue;
        }
        uint64_t at = t->target;
        if (at < now + CIA_MIN_ALARM_US)
            at = now + CIA_MIN_ALARM_US;
        if (!hardware_alarm_set_target(t->alarm, from_us_since_boot(at)))
            return; // alarm is set
        // missed it, go around
    }
    hardware_alarm_cancel(t->alarm);
}

static void cia_alarm_callback(uint alarm_num)
{
    cia_schedule((uint)cia_ta.alarm == alarm_num ? &cia_ta : &cia_tb);
}

// Freeze the counter and stop counting microseconds
static void cia_halt(cia_timer_t *t)
{
    if (t->timed)
    {
        t->counter = cia_count(t);
        t->timed = false;
        hardware_alarm_cancel(t->alarm);
    }
}

// Start counting microseconds if the timer is set to
static void cia_resume(cia_timer_t *t)
{
    if (!cia_counts_us(t))
        return;
    t->target = time_us_64() + t->counter + 1;
    t->timed = true;
    cia_schedule(t);
}

static void cia_write_cr(cia_timer_t *t, uint8_t data)
{
    cia_halt(t);
    t->cr = data & ~CIA_CR_FORCE_LOAD;
    if (data & CIA_CR_FORCE_LOAD)
        t->counter = t->latch;
    cia_resume(t);
}

static void cia_write_latch_hi(cia_timer_t *t, uint8_t data)
{
    t->latch = (uint16_t)(data << 8) | (t->latch & 0x00FF);
    // a stopped timer loads the counter too
    if (!(t->cr & CIA_CR_START))
        t->counter = t->latch;
}

uint8_t cia_reg_read(uint8_t offset)
{
    switch (offset & 0x07)
    {
    case CIA_REG_TALO:
        return (uint8_t)cia_count(&cia_ta);
    case CIA_REG_TAHI:
        return cia_count(&cia_ta) >> 8;
    case CIA_REG_TBLO:
        return (uint8_t)cia_count(&cia_tb);
    case CIA_REG_TBHI:
        return cia_count(&cia_tb) >> 8;
    case CIA_REG_ICR:
    {
        // read to clear
        const uint8_t data = cia_icr;
        cia_icr = 0;
        bus_set_cia_irq(false);
        return data;
    }
    case CIA_REG_CRA:
        return cia_ta.cr;
    case CIA_REG_CRB:
        return cia_tb.cr;
    default:
        return 0xFF;
    }
}

void cia_reg_write(uint8_t offset, uint8_t data)
{
    switch (offset & 0x07)
    {
    case CIA_REG_TALO:
        cia_ta.latch = (cia_ta.latch & 0xFF00) | data;
        break;
    case CIA_REG_TAHI:
        cia_write_latch_hi(&cia_ta, data);
        break;
    case CIA_REG_TBLO:
        cia_tb.latch = (cia_tb.latch & 0xFF00) | data;
        break;
    case CIA_REG_TBHI:
        cia_write_latch_hi(&cia_tb, data);
        break;
    case CIA_REG_ICR:
        if (data & 0x80)
            cia_imr |= data & CIA_IMR_BITS;
        else
            cia_imr &= ~(data & CIA_IMR_BITS);
        cia_update_icr();
        break;
    case CIA_REG_CRA:
        cia_write_cr(&cia_ta, data);
        break;
    case CIA_REG_CRB:
        cia_write_cr(&cia_tb, data);
        break;
    }
}

static void cia_reset_timer(cia_timer_t *t)
{
    hardware_alarm_cancel(t->alarm);
    t->latch = 0xFFFF;
    t->counter = 0;
    t->cr = 0;
    t->timed = false;
}

void cia_stop(void)
{
    // Same interrupt priority as the bus ISR, keep both out
    const uint32_t irq_status = save_and_disable_interrupts();
    cia_reset_timer(&cia_ta);
    cia_reset_timer(&cia_tb);
    cia_icr = 0;
    cia_imr = 0;
    bus_set_cia_irq(false);
    restore_interrupts(irq_status);
}

void cia_init(void)
{
    // Alarm IRQs keep the default priority, the same as the bus ISR,
    // so they never preempt a bus cycle in progress.
    cia_ta.alarm = hardware_alarm_claim_unused(true);
    cia_tb.alarm = hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback(cia_ta.alarm, cia_alarm_callback);
    hardware_alarm_set_callback(cia_tb.alarm, cia_alarm_callback);
    cia_stop();
}
