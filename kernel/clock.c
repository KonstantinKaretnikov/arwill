#include <stdint.h>

#include <arwill/kernel/clock.h>

uint64_t arwill_clock_monotonic_milliseconds(const struct arwill_clock *clock) {
    if (clock == 0 || clock->monotonic_milliseconds == 0) {
        return 0;
    }

    return clock->monotonic_milliseconds(clock->context);
}
