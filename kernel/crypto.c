#include <arwill/kernel/crypto.h>

#include "inner.h"

void arwill_crypto_sha256(const void *data, size_t length, uint8_t output[arwill_sha256_size]) {
    br_sha256_context context;

    br_sha256_init(&context);
    br_sha256_update(&context, data, length);
    br_sha256_out(&context, output);
}
