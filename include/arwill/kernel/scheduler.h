#ifndef ARWILL_KERNEL_SCHEDULER_H
#define ARWILL_KERNEL_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_scheduler_slot_capacity = 2
};

struct arwill_scheduler_slot_stats {
    const char *name;
    uint64_t ticks;
};

struct arwill_scheduler_stats {
    const char *name;
    uint64_t ticks;
    size_t slot_count;
    size_t current_slot;
    struct arwill_scheduler_slot_stats slots[arwill_scheduler_slot_capacity];
};

void arwill_scheduler_init(void);

void arwill_scheduler_tick(int user_mode);

void arwill_scheduler_stats(struct arwill_scheduler_stats *stats);

#endif
