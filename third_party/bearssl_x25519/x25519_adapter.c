#include "inner.h"
#include "x25519_adapter.h"

enum {
    x25519_size = 32,
    bearssl_curve25519_id = 29,
};

int bearssl_x25519_mul(
    uint8_t output[x25519_size],
    const uint8_t scalar[x25519_size],
    const uint8_t point[x25519_size]
) {
    for (size_t index = 0; index < x25519_size; index++) {
        output[index] = point[index];
    }

    return br_ec_c25519_m31.mul(
        output,
        x25519_size,
        scalar,
        x25519_size,
        bearssl_curve25519_id
    ) != 0U;
}
