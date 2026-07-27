#ifndef ARWILL_USER_HTTP_H
#define ARWILL_USER_HTTP_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_http_host_capacity = 64,
    arwill_http_path_capacity = 256
};

enum arwill_http_result {
    arwill_http_ok = 0,
    arwill_http_invalid = -1,
    arwill_http_unsupported = -2,
    arwill_http_too_large = -3
};

enum arwill_http_method {
    arwill_http_method_get,
    arwill_http_method_post
};

enum arwill_http_scheme {
    arwill_http_scheme_http,
    arwill_http_scheme_https
};

struct arwill_http_url {
    char host[arwill_http_host_capacity];
    char path[arwill_http_path_capacity];
    uint16_t port;
    enum arwill_http_scheme scheme;
};

int arwill_http_parse_url(const char *text, struct arwill_http_url *url);
int arwill_http_build_request(
    enum arwill_http_method method,
    const struct arwill_http_url *url,
    const uint8_t *body,
    size_t body_length,
    uint8_t *output,
    size_t capacity,
    size_t *length
);

#endif
