#ifndef ARWILL_BEARSSL_ECDSA_INNER_H
#define ARWILL_BEARSSL_ECDSA_INNER_H

#include "../bearssl_sha256/inner.h"
#include "../bearssl_x25519/inner.h"

#define BR_MAX_EC_SIZE 256
#define BR_EC_secp256r1 23
#define BR_HASHDESC_OUT_OFF 8
#define BR_HASHDESC_OUT_MASK 0x7fU
#define BR_HASHDESC_LBLEN_OFF 23
#define BR_HASHDESC_LBLEN_MASK 0x0fU

typedef union {
    const br_hash_class *vtable;
    br_sha256_context sha256;
} br_hash_compat_context;

typedef struct {
    const br_hash_class *dig_vtable;
    unsigned char ksi[64];
    unsigned char kso[64];
} br_hmac_key_context;

typedef struct {
    br_hash_compat_context dig;
    unsigned char kso[64];
    size_t out_len;
} br_hmac_context;

typedef struct br_prng_class_ br_prng_class;
struct br_prng_class_ {
    size_t context_size;
    void (*init)(const br_prng_class **ctx, const void *params,
        const void *seed, size_t seed_length);
    void (*generate)(const br_prng_class **ctx, void *output, size_t length);
    void (*update)(const br_prng_class **ctx, const void *seed, size_t seed_length);
};

typedef struct {
    const br_prng_class *vtable;
    unsigned char K[64];
    unsigned char V[64];
    const br_hash_class *digest_class;
} br_hmac_drbg_context;

typedef struct {
    int curve;
    unsigned char *x;
    size_t xlen;
} br_ec_private_key;

typedef struct {
    int curve;
    const unsigned char *order;
    size_t order_len;
    const unsigned char *generator;
    size_t generator_len;
} br_ec_curve_def;

extern const br_ec_curve_def br_secp256r1;
extern const br_ec_impl br_ec_p256_m31;
extern const br_prng_class br_hmac_drbg_vtable;

static inline size_t br_digest_size(const br_hash_class *digest_class) {
    return (size_t)(digest_class->desc >> 8U) & 0x7fU;
}

static inline uint32_t GT(uint32_t left, uint32_t right) {
    const uint32_t difference = right - left;

    return (difference ^ ((left ^ right) & (left ^ difference))) >> 31U;
}

static inline int32_t CMP(uint32_t left, uint32_t right) {
    return (int32_t)GT(left, right) | -(int32_t)GT(right, left);
}

static inline uint32_t BIT_LENGTH(uint32_t value) {
    uint32_t length = NEQ(value, 0U);
    uint32_t control = GT(value, 0xffffU);
    value = MUX(control, value >> 16U, value);
    length += control << 4U;
    control = GT(value, 0xffU);
    value = MUX(control, value >> 8U, value);
    length += control << 3U;
    control = GT(value, 0x0fU);
    value = MUX(control, value >> 4U, value);
    length += control << 2U;
    control = GT(value, 0x03U);
    value = MUX(control, value >> 2U, value);
    length += control << 1U;
    return length + GT(value, 1U);
}

#define MUL31_lo(x, y) (((uint32_t)(x) * (uint32_t)(y)) & 0x7fffffffU)
#define LT(x, y) GT((y), (x))
#define GE(x, y) NOT(GT((y), (x)))

static inline void *br_compat_memmove(void *destination, const void *source, size_t length) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    if (destination_bytes < source_bytes) {
        for (size_t index = 0; index < length; index++) {
            destination_bytes[index] = source_bytes[index];
        }
    } else {
        for (size_t index = length; index > 0U; index--) {
            destination_bytes[index - 1U] = source_bytes[index - 1U];
        }
    }
    return destination;
}

#define memmove br_compat_memmove

uint32_t br_divrem(uint32_t high, uint32_t low, uint32_t divisor, uint32_t *remainder);
static inline uint32_t br_rem(uint32_t high, uint32_t low, uint32_t divisor) {
    uint32_t remainder;
    br_divrem(high, low, divisor, &remainder);
    return remainder;
}
static inline uint32_t br_div(uint32_t high, uint32_t low, uint32_t divisor) {
    uint32_t remainder;
    return br_divrem(high, low, divisor, &remainder);
}

static inline void br_i31_zero(uint32_t *value, uint32_t bit_length) {
    *value++ = bit_length;
    memset(value, 0, ((bit_length + 31U) >> 5U) * sizeof(*value));
}

uint32_t br_i31_bit_length(uint32_t *value, size_t length);
uint32_t br_i31_add(uint32_t *a, const uint32_t *b, uint32_t control);
uint32_t br_i31_sub(uint32_t *a, const uint32_t *b, uint32_t control);
void br_i31_decode(uint32_t *x, const void *source, size_t length);
uint32_t br_i31_decode_mod(uint32_t *x, const void *source, size_t length,
    const uint32_t *modulus);
void br_i31_encode(void *destination, size_t length, const uint32_t *x);
void br_i31_from_monty(uint32_t *x, const uint32_t *modulus, uint32_t m0i);
void br_i31_to_monty(uint32_t *x, const uint32_t *modulus);
uint32_t br_i31_iszero(const uint32_t *x);
void br_i31_modpow(uint32_t *x, const unsigned char *exponent, size_t exponent_length,
    const uint32_t *modulus, uint32_t m0i, uint32_t *temporary1, uint32_t *temporary2);
void br_i31_montymul(uint32_t *destination, const uint32_t *x, const uint32_t *y,
    const uint32_t *modulus, uint32_t m0i);
void br_i31_muladd_small(uint32_t *x, uint32_t value, const uint32_t *modulus);
uint32_t br_i31_ninv31(uint32_t value);
void br_i31_rshift(uint32_t *x, int count);

void br_ecdsa_i31_bits2int(uint32_t *x, const void *source, size_t length,
    uint32_t encoded_bit_length);
size_t br_ecdsa_i31_sign_raw(const br_ec_impl *implementation,
    const br_hash_class *hash, const void *hash_value,
    const br_ec_private_key *private_key, void *signature);

void br_hmac_key_init(br_hmac_key_context *context, const br_hash_class *digest,
    const void *key, size_t key_length);
void br_hmac_init(br_hmac_context *context, const br_hmac_key_context *key,
    size_t output_length);
void br_hmac_update(br_hmac_context *context, const void *data, size_t length);
size_t br_hmac_out(const br_hmac_context *context, void *output);
void br_hmac_drbg_init(br_hmac_drbg_context *context,
    const br_hash_class *digest, const void *seed, size_t length);
void br_hmac_drbg_generate(br_hmac_drbg_context *context, void *output, size_t length);
void br_hmac_drbg_update(br_hmac_drbg_context *context, const void *seed, size_t length);

#endif
