/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "./cia.h"
#include "sys/ria.h"
#include <hardware/irq.h>
#include <hardware/timer.h>
#include <pico/stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_LED)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

typedef struct
{
    uint16_t latch;
    uint16_t counter;
    uint8_t control;
    uint alarm_num;
} cia_timer_t;

#define CIA_TIMER_MASK_START      (1 << 0) // 1 = START, 0 = STOP
#define CIA_TIMER_MASK_RUN_MODE   (1 << 3) // 0 = CONTINUOUS, 1 = ONE-SHOT
#define CIA_TIMER_MASK_FORCE_LOAD (1 << 4) // 1 = FORCE LOAD Latch into Counter

#define CIA_TIMER_CRB_INMODE_MASK (3 << 5)
#define CIA_TIMER_CRB_INMODE_PHI2 (0 << 5) // Count pulses on PHI2
#define CIA_TIMER_CRB_INMODE_TA   (2 << 5) // Count Timer A underflows

typedef struct
{
    cia_timer_t A;
    cia_timer_t B;
    // Interrupt Control Register
    uint8_t icr_flags; // each interrupt sets an interrupt bit regardless of the MASK
    uint8_t icr_mask;
} cia_t;

#define CIA_ICR_SET_BIT (1 << 7) // Bit 7: Set/Clear
#define CIA_ICR_INT_TA  (1 << 0) // Bit 0: Timer A Interrupt
#define CIA_ICR_INT_TB  (1 << 1) // Bit 1: Timer B Interrupt
#define CIA_ICR_INT_IR  (1 << 7) // Bit 7: Interrupt Ready

static volatile cia_t CIA;

void __isr __not_in_flash_func(cia_timer_irq_handler)(uint alarm_num)
{
    volatile cia_timer_t *t = (alarm_num == CIA.A.alarm_num) ? &CIA.A : &CIA.B;

    CIA.icr_flags |= (alarm_num == CIA.A.alarm_num) ? CIA_ICR_INT_TA : CIA_ICR_INT_TB;
    CIA.icr_flags |= CIA_ICR_INT_IR;
    if (CIA.icr_mask & CIA.icr_flags)
        ria_set_irq(RIA_IRQ_SOURCE_CIA);

    t->counter = 0;

    // One-shot?
    if (t->control & CIA_TIMER_MASK_RUN_MODE)
    {
        t->control &= ~CIA_TIMER_MASK_START;
    }
    else
    {
        // Continuous mode: Schedule next alarm
        if (hardware_alarm_set_target(t->alarm_num, timer_hw->alarm[alarm_num] + t->latch))
        {
            // Missed the deadline - retry from NOW()
            hardware_alarm_set_target(t->alarm_num, timer_hw->timerawl + t->counter);
        }
    }

    // Clear the interrupt
    hw_clear_bits(&timer_hw->intr, 1u << alarm_num);
}

static uint16_t cia_timer_count(volatile cia_timer_t *t)
{
    uint32_t now = timer_hw->timerawl;
    uint32_t target = timer_hw->alarm[t->alarm_num];

    // If we've passed the target but the ISR hasn't run yet
    if (now >= target)
        return 0;

    // Remaining microseconds = CIA cycles
    return (uint16_t)(target - now);
}

void cia_timer_start(volatile cia_timer_t *t)
{
    t->control |= CIA_TIMER_MASK_START;

    hardware_alarm_set_target(t->alarm_num, timer_hw->timerawl + t->counter);
}

void cia_timer_stop(volatile cia_timer_t *t)
{
    hardware_alarm_cancel(t->alarm_num);
    t->control &= ~CIA_TIMER_MASK_START;
    t->counter = cia_timer_count(t);
}

static inline bool cia_timer_running(volatile cia_timer_t *t)
{
    return (t->control & CIA_TIMER_MASK_START);
}

uint16_t cia_get_count(enum cia_timer_id t_id)
{
    volatile cia_timer_t *t = (t_id == CIA_A) ? &CIA.A : &CIA.B;

    if (cia_timer_running(t))
        return cia_timer_count(t);

    // Timer is stopped - return latched counter value
    return t->counter;
}

uint8_t cia_get_icr(void)
{
    uint8_t flags = CIA.icr_flags;
    CIA.icr_flags = 0; // Clear all flags on read
    ria_clear_irq(RIA_IRQ_SOURCE_CIA);
    return flags;
}

uint8_t cia_get_control(enum cia_timer_id t_id)
{
    volatile cia_timer_t *t = (t_id == CIA_A) ? &CIA.A : &CIA.B;
    return t->control;
}

void cia_set_count_lo(enum cia_timer_id t_id, uint8_t value)
{
    volatile cia_timer_t *t = (t_id == CIA_A) ? &CIA.A : &CIA.B;
    t->latch = (t->latch & 0xFF00) | value;
}

void cia_set_count_hi(enum cia_timer_id t_id, uint8_t value)
{
    volatile cia_timer_t *t = (t_id == CIA_A) ? &CIA.A : &CIA.B;
    t->latch = (t->latch & 0x00FF) | (uint16_t)((uint16_t)value << 8);

    // If timer is stopped, load latch into counter
    if (!cia_timer_running(t))
        t->counter = t->latch;
}

void cia_set_icr(uint8_t value)
{
    if (value & CIA_ICR_SET_BIT)
    {
        CIA.icr_mask |= (value & (CIA_ICR_INT_TA | CIA_ICR_INT_TB));
    }
    else
    {
        CIA.icr_mask &= ~value;
    }
}

void cia_set_control(enum cia_timer_id t_id, uint8_t value)
{
    volatile cia_timer_t *t = (t_id == CIA_A) ? &CIA.A : &CIA.B;

    if (value & CIA_TIMER_MASK_FORCE_LOAD)
        t->counter = t->latch;

    t->control = (t->control & ~CIA_TIMER_MASK_RUN_MODE) | (value & CIA_TIMER_MASK_RUN_MODE);

    if (value & CIA_TIMER_MASK_START)
    {
        if (!cia_timer_running(t))
            cia_timer_start(t);
    }
    else
    {
        if (cia_timer_running(t))
            cia_timer_stop(t);
    }
}

void cia_init(void)
{
    memset(&CIA, 0, sizeof(CIA));

    // Claim two hardware alarms
    CIA.A.alarm_num = hardware_alarm_claim_unused(true);
    CIA.B.alarm_num = hardware_alarm_claim_unused(true);

    // Setup IRQs
    hardware_alarm_set_callback(CIA.A.alarm_num, cia_timer_irq_handler);
    hardware_alarm_set_callback(CIA.B.alarm_num, cia_timer_irq_handler);

    // Set Priority 0 (Highest)
    irq_set_priority(hardware_alarm_get_irq_num(CIA.A.alarm_num), 0);
    irq_set_priority(hardware_alarm_get_irq_num(CIA.B.alarm_num), 0);
}

void cia_task(void)
{
}
