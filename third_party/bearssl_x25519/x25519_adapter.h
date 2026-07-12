#ifndef ARWILL_BEARSSL_X25519_ADAPTER_H
#define ARWILL_BEARSSL_X25519_ADAPTER_H

#include <stdint.h>

int bearssl_x25519_mul(
    uint8_t output[32],
    const uint8_t scalar[32],
    const uint8_t point[32]
);

#endif
