#ifndef ARWILL_KERNEL_CRYPTO_H
#define ARWILL_KERNEL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_sha256_size = 32,
    arwill_x25519_size = 32,
    arwill_p256_scalar_size = 32,
    arwill_p256_point_size = 65,
    arwill_p256_signature_size = 64,
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
int arwill_crypto_p256_public(
    uint8_t output[arwill_p256_point_size],
    const uint8_t scalar[arwill_p256_scalar_size]
);
int arwill_crypto_p256_sign(
    uint8_t signature[arwill_p256_signature_size],
    const uint8_t private_key[arwill_p256_scalar_size],
    const uint8_t hash[arwill_sha256_size]
);

#endif
