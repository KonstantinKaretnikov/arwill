#ifndef ARWILL_KERNEL_TEXT_H
#define ARWILL_KERNEL_TEXT_H

#include <stddef.h>

size_t arwill_text_length(const char *text);
int arwill_text_equals(const char *left, const char *right);
int arwill_text_starts_with(const char *text, const char *prefix);
int arwill_text_starts_with_sized(
    const char *text,
    const char *prefix,
    size_t prefix_length
);

#endif
