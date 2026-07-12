#ifndef ARWILL_KERNEL_INTERRUPTS_H
#define ARWILL_KERNEL_INTERRUPTS_H

#include <stdint.h>

struct arwill_interrupt_stats {
    int idt_loaded;
    int pic_remapped;
    int timer_configured;
    int enabled;
    uint64_t timer_ticks;
    uint64_t exception_count;
    uint8_t last_exception_vector;
};

struct arwill_interrupts {
    void *context;
    const char *name;
    void (*enable)(void *context);
    void (*stats)(void *context, struct arwill_interrupt_stats *stats);
    void (*trigger_breakpoint)(void *context);
};

void arwill_interrupts_enable(const struct arwill_interrupts *interrupts);

void arwill_interrupts_stats(
    const struct arwill_interrupts *interrupts,
    struct arwill_interrupt_stats *stats
);

void arwill_interrupts_trigger_breakpoint(const struct arwill_interrupts *interrupts);

int arwill_interrupts_wait_for_timer_tick(const struct arwill_interrupts *interrupts);

#endif
