#ifndef ARWILL_KERNEL_CLOCK_H
#define ARWILL_KERNEL_CLOCK_H

#include <stdint.h>

struct arwill_clock {
    const char *name;
    void *context;
    uint64_t (*monotonic_milliseconds)(void *context);
};

uint64_t arwill_clock_monotonic_milliseconds(const struct arwill_clock *clock);

#endif
