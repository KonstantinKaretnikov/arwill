/*
 * Minimal compatibility surface for the unmodified BearSSL 0.6 SHA-256
 * implementation vendored in this directory. This is Arwill-owned glue,
 * not a copy of BearSSL's full internal header.
 */
#ifndef ARWILL_BEARSSL_SHA256_INNER_H
#define ARWILL_BEARSSL_SHA256_INNER_H

#include <stddef.h>
#include <stdint.h>

typedef struct br_hash_class_ br_hash_class;

struct br_hash_class_ {
    size_t context_size;
    uint32_t desc;
    void (*init)(const br_hash_class **ctx);
    void (*update)(const br_hash_class **ctx, const void *data, size_t len);
    void (*out)(const br_hash_class *const *ctx, void *dst);
    uint64_t (*state)(const br_hash_class *const *ctx, void *dst);
    void (*set_state)(const br_hash_class **ctx, const void *state, uint64_t count);
};

#define BR_HASHDESC_ID(id) ((uint32_t)(id))
#define BR_HASHDESC_OUT(size) ((uint32_t)(size) << 8U)
#define BR_HASHDESC_STATE(size) ((uint32_t)(size) << 15U)
#define BR_HASHDESC_LBLEN(size) ((uint32_t)(size) << 23U)
#define BR_HASHDESC_MD_PADDING ((uint32_t)1U << 28U)
#define BR_HASHDESC_MD_PADDING_BE ((uint32_t)1U << 30U)

#define br_sha224_ID 3
#define br_sha256_ID 4

typedef struct {
    const br_hash_class *vtable;
    unsigned char buf[64];
    uint64_t count;
    uint32_t val[8];
} br_sha224_context;

typedef br_sha224_context br_sha256_context;

extern const uint32_t br_sha224_IV[8];
extern const uint32_t br_sha256_IV[8];
extern const br_hash_class br_sha224_vtable;
extern const br_hash_class br_sha256_vtable;

void br_sha2small_round(const unsigned char *buf, uint32_t *val);
void br_sha224_init(br_sha224_context *ctx);
void br_sha224_update(br_sha224_context *ctx, const void *data, size_t len);
void br_sha224_out(const br_sha224_context *ctx, void *out);
uint64_t br_sha224_state(const br_sha224_context *ctx, void *out);
void br_sha224_set_state(br_sha224_context *ctx, const void *state, uint64_t count);
void br_sha256_init(br_sha256_context *ctx);
void br_sha256_out(const br_sha256_context *ctx, void *out);

#define br_sha256_update br_sha224_update
#define br_sha256_state br_sha224_state
#define br_sha256_set_state br_sha224_set_state

void br_range_dec32be(uint32_t *values, size_t count, const void *source);
void br_range_enc32be(void *destination, const uint32_t *values, size_t count);

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

static inline uint32_t br_dec32be(const void *source) {
    const unsigned char *bytes = source;

    return ((uint32_t)bytes[0] << 24U)
        | ((uint32_t)bytes[1] << 16U)
        | ((uint32_t)bytes[2] << 8U)
        | (uint32_t)bytes[3];
}

static inline void br_enc32be(void *destination, uint32_t value) {
    unsigned char *bytes = destination;

    bytes[0] = (unsigned char)(value >> 24U);
    bytes[1] = (unsigned char)(value >> 16U);
    bytes[2] = (unsigned char)(value >> 8U);
    bytes[3] = (unsigned char)value;
}

static inline void br_enc64be(void *destination, uint64_t value) {
    unsigned char *bytes = destination;

    br_enc32be(bytes, (uint32_t)(value >> 32U));
    br_enc32be(bytes + 4, (uint32_t)value);
}

#endif
