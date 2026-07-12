#ifndef ARWILL_KERNEL_INPUT_H
#define ARWILL_KERNEL_INPUT_H

#include <stdint.h>

struct arwill_input {
    void *context;
    uint8_t (*read_byte)(void *context);
    int (*try_read_byte)(void *context, uint8_t *byte);
};

uint8_t arwill_input_read_byte(const struct arwill_input *input);
int arwill_input_try_read_byte(const struct arwill_input *input, uint8_t *byte);

#endif
