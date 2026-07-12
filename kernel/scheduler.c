#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/scheduler.h>

struct scheduler_slot {
    const char *name;
    volatile uint64_t ticks;
};

static volatile uint64_t scheduler_ticks;
static volatile size_t scheduler_current_slot;
static struct scheduler_slot scheduler_slots[arwill_scheduler_slot_capacity];

void arwill_scheduler_init(void) {
    scheduler_ticks = 0;
    scheduler_current_slot = 0;

    scheduler_slots[0].name = "shell";
    scheduler_slots[0].ticks = 0;
    scheduler_slots[1].name = "idle";
    scheduler_slots[1].ticks = 0;
}

void arwill_scheduler_tick(void) {
    scheduler_ticks++;
    scheduler_slots[scheduler_current_slot].ticks++;
    scheduler_current_slot++;

    if (scheduler_current_slot >= arwill_scheduler_slot_capacity) {
        scheduler_current_slot = 0;
    }
}

void arwill_scheduler_stats(struct arwill_scheduler_stats *stats) {
    if (stats == 0) {
        return;
    }

    stats->name = "timer tick round-robin foundation";
    stats->ticks = scheduler_ticks;
    stats->slot_count = arwill_scheduler_slot_capacity;
    stats->current_slot = scheduler_current_slot;

    for (size_t index = 0; index < arwill_scheduler_slot_capacity; index++) {
        stats->slots[index].name = scheduler_slots[index].name;
        stats->slots[index].ticks = scheduler_slots[index].ticks;
    }
}
