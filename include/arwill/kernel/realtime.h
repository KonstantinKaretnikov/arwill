#ifndef ARWILL_KERNEL_REALTIME_H
#define ARWILL_KERNEL_REALTIME_H

#include <stdint.h>

struct arwill_realtime {
    void *context;
    int (*unix_seconds)(void *context, uint64_t *seconds);
};

int arwill_realtime_unix_seconds(
    const struct arwill_realtime *realtime,
    uint64_t *seconds
);

int arwill_realtime_calendar_to_unix(
    unsigned year,
    unsigned month,
    unsigned day,
    unsigned hour,
    unsigned minute,
    unsigned second,
    uint64_t *seconds
);

#endif
