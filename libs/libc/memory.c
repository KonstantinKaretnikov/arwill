#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    for (size_t index = 0U; index < length; index++) {
        output[index] = input[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    if (output < input) {
        for (size_t index = 0U; index < length; index++) {
            output[index] = input[index];
        }
    } else if (output > input) {
        for (size_t index = length; index != 0U; index--) {
            output[index - 1U] = input[index - 1U];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *output = destination;
    for (size_t index = 0U; index < length; index++) {
        output[index] = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *first = left;
    const unsigned char *second = right;
    for (size_t index = 0U; index < length; index++) {
        if (first[index] != second[index]) {
            return first[index] < second[index] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}
