#include <arwill/kernel/text.h>

size_t arwill_text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int arwill_text_equals(const char *left, const char *right) {
    size_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        index++;
    }
    return left[index] == right[index];
}

int arwill_text_starts_with(const char *text, const char *prefix) {
    return arwill_text_starts_with_sized(
        text, prefix, arwill_text_length(prefix)
    );
}

int arwill_text_starts_with_sized(
    const char *text,
    const char *prefix,
    size_t prefix_length
) {
    for (size_t index = 0; index < prefix_length; index++) {
        if (text[index] != prefix[index]) {
            return 0;
        }
    }
    return 1;
}
