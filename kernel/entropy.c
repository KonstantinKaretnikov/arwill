#include <arwill/kernel/entropy.h>

int arwill_entropy_fill(
    const struct arwill_entropy *entropy,
    uint8_t *output,
    size_t length
) {
    if (entropy == 0 || entropy->fill == 0 ||
        (output == 0 && length != 0U)) {
        return 0;
    }
    return entropy->fill(entropy->context, output, length);
}
