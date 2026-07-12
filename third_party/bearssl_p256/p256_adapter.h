#ifndef ARWILL_BEARSSL_P256_ADAPTER_H
#define ARWILL_BEARSSL_P256_ADAPTER_H

#include <stdint.h>

int bearssl_p256_public(uint8_t output[65], const uint8_t scalar[32]);

#endif
