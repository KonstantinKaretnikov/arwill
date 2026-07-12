/* Minimal Arwill compatibility surface for BearSSL 0.6 X25519 sources. */
#ifndef ARWILL_BEARSSL_X25519_INNER_H
#define ARWILL_BEARSSL_X25519_INNER_H

#include <stddef.h>
#include <stdint.h>

#define BR_NO_ARITH_SHIFT 1
#define MUL31(x, y) ((uint64_t)(x) * (uint64_t)(y))
#define MUL15(x, y) ((uint32_t)(x) * (uint32_t)(y))

typedef struct {
    uint32_t supported_curves;
    const unsigned char *(*generator)(int curve, size_t *length);
    const unsigned char *(*order)(int curve, size_t *length);
    size_t (*xoff)(int curve, size_t *length);
    uint32_t (*mul)(
        unsigned char *point,
        size_t point_length,
        const unsigned char *scalar,
        size_t scalar_length,
        int curve
    );
    size_t (*mulgen)(
        unsigned char *result,
        const unsigned char *scalar,
        size_t scalar_length,
        int curve
    );
    uint32_t (*muladd)(
        unsigned char *point_a,
        const unsigned char *point_b,
        size_t point_length,
        const unsigned char *scalar_x,
        size_t scalar_x_length,
        const unsigned char *scalar_y,
        size_t scalar_y_length,
        int curve
    );
} br_ec_impl;

extern const br_ec_impl br_ec_c25519_m31;

static inline uint32_t NOT(uint32_t control) {
    return control ^ 1U;
}

static inline uint32_t MUX(uint32_t control, uint32_t if_true, uint32_t if_false) {
    return if_false ^ ((uint32_t)(0U - control) & (if_true ^ if_false));
}

static inline uint32_t EQ(uint32_t left, uint32_t right) {
    const uint32_t difference = left ^ right;

    return NOT((difference | (uint32_t)(0U - difference)) >> 31U);
}

static inline uint32_t NEQ(uint32_t left, uint32_t right) {
    const uint32_t difference = left ^ right;

    return (difference | (uint32_t)(0U - difference)) >> 31U;
}

void br_ccopy(uint32_t control, void *destination, const void *source, size_t length);

#define CCOPY br_ccopy

#ifndef ARWILL_BEARSSL_COMPAT_MEMORY
#define ARWILL_BEARSSL_COMPAT_MEMORY
static inline void *br_compat_memcpy(void *destination, const void *source, size_t length) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    for (size_t index = 0; index < length; index++) {
        destination_bytes[index] = source_bytes[index];
    }

    return destination;
}

static inline void *br_compat_memset(void *destination, int value, size_t length) {
    unsigned char *destination_bytes = destination;

    for (size_t index = 0; index < length; index++) {
        destination_bytes[index] = (unsigned char)value;
    }

    return destination;
}

#define memcpy br_compat_memcpy
#define memset br_compat_memset
#endif

#endif
