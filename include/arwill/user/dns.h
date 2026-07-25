#ifndef ARWILL_USER_DNS_H
#define ARWILL_USER_DNS_H

#include <stddef.h>
#include <stdint.h>

enum arwill_dns_result {
    arwill_dns_ok = 0,
    arwill_dns_invalid = -1,
    arwill_dns_too_large = -2,
    arwill_dns_not_found = -3,
    arwill_dns_server_failure = -4
};

int arwill_dns_build_a_query(
    const char *host,
    uint16_t identifier,
    uint8_t *output,
    size_t capacity,
    size_t *length
);
int arwill_dns_parse_a_response(
    const uint8_t *message,
    size_t length,
    uint16_t identifier,
    uint32_t *address
);

#endif
