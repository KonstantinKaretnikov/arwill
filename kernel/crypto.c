#include <arwill/kernel/crypto.h>

#include "inner.h"
#include "x25519_adapter.h"
#include "p256_adapter.h"
#include "ecdsa_adapter.h"

void arwill_crypto_sha256(const void *data, size_t length, uint8_t output[arwill_sha256_size]) {
    br_sha256_context context;

    br_sha256_init(&context);
    br_sha256_update(&context, data, length);
    br_sha256_out(&context, output);
}

int arwill_crypto_x25519(
    uint8_t output[arwill_x25519_size],
    const uint8_t scalar[arwill_x25519_size],
    const uint8_t point[arwill_x25519_size]
) {
    uint8_t nonzero = 0U;

    if (!bearssl_x25519_mul(output, scalar, point)) {
        return 0;
    }

    for (size_t index = 0; index < arwill_x25519_size; index++) {
        nonzero |= output[index];
    }

    return nonzero != 0U;
}

int arwill_crypto_x25519_public(
    uint8_t output[arwill_x25519_size],
    const uint8_t scalar[arwill_x25519_size]
) {
    static const uint8_t generator[arwill_x25519_size] = { 9U };

    return arwill_crypto_x25519(output, scalar, generator);
}

int arwill_crypto_p256_public(
    uint8_t output[arwill_p256_point_size],
    const uint8_t scalar[arwill_p256_scalar_size]
) {
    return bearssl_p256_public(output, scalar);
}

int arwill_crypto_p256_sign(
    uint8_t signature[arwill_p256_signature_size],
    const uint8_t private_key[arwill_p256_scalar_size],
    const uint8_t hash[arwill_sha256_size]
) {
    return bearssl_p256_sign(signature, private_key, hash);
}
