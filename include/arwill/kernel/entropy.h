#ifndef ARWILL_KERNEL_ENTROPY_H
#define ARWILL_KERNEL_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

const char *arwill_entropy_source_name(void);
int arwill_entropy_available(void);
int arwill_entropy_fill(uint8_t *output, size_t length);

#endif
