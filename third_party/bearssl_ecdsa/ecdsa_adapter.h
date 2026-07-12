#ifndef ARWILL_BEARSSL_ECDSA_ADAPTER_H
#define ARWILL_BEARSSL_ECDSA_ADAPTER_H

#include <stdint.h>

int bearssl_p256_sign(uint8_t signature[64], const uint8_t private_key[32],
    const uint8_t hash[32]);

#endif
