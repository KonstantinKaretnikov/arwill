#ifndef ARWILL_USER_TLS_H
#define ARWILL_USER_TLS_H

#include <stddef.h>
#include <stdint.h>

enum arwill_tls_result {
    arwill_tls_ok = 0,
    arwill_tls_invalid = -1,
    arwill_tls_handshake_failed = -2,
    arwill_tls_transport_failed = -3,
    arwill_tls_output_failed = -4
};

struct arwill_tls_transport {
    void *context;
    int (*read)(void *context, uint8_t *output, size_t capacity);
    int (*write)(void *context, const uint8_t *input, size_t length);
};

typedef int (*arwill_tls_receive_fn)(
    void *context,
    const uint8_t *input,
    size_t length
);

int arwill_tls_exchange(
    const char *host,
    uint64_t unix_seconds,
    const uint8_t *entropy,
    size_t entropy_length,
    const struct arwill_tls_transport *transport,
    const uint8_t *request,
    size_t request_length,
    arwill_tls_receive_fn receive,
    void *receive_context,
    int *tls_error
);

#endif
