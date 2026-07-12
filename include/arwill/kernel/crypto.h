#ifndef ARWILL_KERNEL_CRYPTO_H
#define ARWILL_KERNEL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_sha256_size = 32,
    arwill_x25519_size = 32,
};

void arwill_crypto_sha256(const void *data, size_t length, uint8_t output[arwill_sha256_size]);
int arwill_crypto_x25519(
    uint8_t output[arwill_x25519_size],
    const uint8_t scalar[arwill_x25519_size],
    const uint8_t point[arwill_x25519_size]
);
int arwill_crypto_x25519_public(
    uint8_t output[arwill_x25519_size],
    const uint8_t scalar[arwill_x25519_size]
);

#endif
