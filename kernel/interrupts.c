#include <stdint.h>

#include <arwill/kernel/interrupts.h>

void arwill_interrupts_enable(const struct arwill_interrupts *interrupts) {
    if (interrupts == 0 || interrupts->enable == 0) {
        return;
    }

    interrupts->enable(interrupts->context);
}

void arwill_interrupts_stats(
    const struct arwill_interrupts *interrupts,
    struct arwill_interrupt_stats *stats
) {
    if (stats == 0) {
        return;
    }

    stats->idt_loaded = 0;
    stats->pic_remapped = 0;
    stats->timer_configured = 0;
    stats->enabled = 0;
    stats->timer_ticks = 0;
    stats->exception_count = 0;
    stats->last_exception_vector = 0;

    if (interrupts == 0 || interrupts->stats == 0) {
        return;
    }

    interrupts->stats(interrupts->context, stats);
}

void arwill_interrupts_trigger_breakpoint(const struct arwill_interrupts *interrupts) {
    if (interrupts == 0 || interrupts->trigger_breakpoint == 0) {
        return;
    }

    interrupts->trigger_breakpoint(interrupts->context);
}

int arwill_interrupts_wait_for_timer_tick(const struct arwill_interrupts *interrupts) {
    enum {
        wait_iterations = 10000000
    };

    struct arwill_interrupt_stats before;
    struct arwill_interrupt_stats after;

    arwill_interrupts_stats(interrupts, &before);

    for (uint32_t index = 0; index < wait_iterations; index++) {
        arwill_interrupts_stats(interrupts, &after);

        if (after.timer_ticks > before.timer_ticks) {
            return 1;
        }
    }

    return 0;
}
