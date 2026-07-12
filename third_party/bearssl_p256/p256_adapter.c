#include "inner.h"
#include "p256_adapter.h"

enum {
    p256_scalar_size = 32,
    p256_point_size = 65,
    bearssl_secp256r1_id = 23,
};

int bearssl_p256_public(
    uint8_t output[p256_point_size],
    const uint8_t scalar[p256_scalar_size]
) {
    return br_ec_p256_m31.mulgen(
        output,
        scalar,
        p256_scalar_size,
        bearssl_secp256r1_id
    ) == p256_point_size;
}
