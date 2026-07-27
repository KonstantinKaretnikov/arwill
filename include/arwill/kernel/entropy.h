#ifndef ARWILL_KERNEL_ENTROPY_H
#define ARWILL_KERNEL_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

struct arwill_entropy {
    void *context;
    int (*fill)(void *context, uint8_t *output, size_t length);
};

int arwill_entropy_fill(
    const struct arwill_entropy *entropy,
    uint8_t *output,
    size_t length
);

#endif
