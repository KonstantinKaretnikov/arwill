#include "inner.h"
#include "ecdsa_adapter.h"

int bearssl_p256_sign(
    uint8_t signature[64],
    const uint8_t private_key[32],
    const uint8_t hash[32]
) {
    br_ec_private_key key;

    key.curve = BR_EC_secp256r1;
    key.x = (unsigned char *)(uintptr_t)private_key;
    key.xlen = 32U;
    return br_ecdsa_i31_sign_raw(
        &br_ec_p256_m31,
        &br_sha256_vtable,
        hash,
        &key,
        signature
    ) == 64U;
}
