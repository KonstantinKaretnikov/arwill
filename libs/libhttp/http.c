#include <arwill/user/http.h>

static int append_byte(
    uint8_t *output,
    size_t capacity,
    size_t *length,
    uint8_t byte
) {
    if (*length >= capacity) {
        return 0;
    }
    output[*length] = byte;
    (*length)++;
    return 1;
}

static int append_text(
    uint8_t *output,
    size_t capacity,
    size_t *length,
    const char *text
) {
    size_t index = 0;
    while (text[index] != '\0') {
        if (!append_byte(output, capacity, length, (uint8_t)text[index])) {
            return 0;
        }
        index++;
    }
    return 1;
}

static int append_decimal(
    uint8_t *output,
    size_t capacity,
    size_t *length,
    size_t value
) {
    char reversed[24];
    size_t count = 0;
    do {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) {
        count--;
        if (!append_byte(output, capacity, length, (uint8_t)reversed[count])) {
            return 0;
        }
    }
    return 1;
}

static int valid_host_character(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '-' || value == '.';
}

int arwill_http_parse_url(const char *text, struct arwill_http_url *url) {
    static const char http_prefix[] = "http://";
    static const char https_prefix[] = "https://";
    if (text == 0 || url == 0) {
        return arwill_http_invalid;
    }
    size_t index = 0;
    while (http_prefix[index] != '\0' &&
        text[index] == http_prefix[index]) {
        index++;
    }
    if (http_prefix[index] == '\0') {
        url->scheme = arwill_http_scheme_http;
        url->port = 80U;
    } else {
        index = 0;
        while (https_prefix[index] != '\0' &&
            text[index] == https_prefix[index]) {
            index++;
        }
        if (https_prefix[index] != '\0') {
            return text[0] != '\0' ? arwill_http_unsupported :
                arwill_http_invalid;
        }
        url->scheme = arwill_http_scheme_https;
        url->port = 443U;
    }

    size_t host_length = 0;
    while (text[index] != '\0' && text[index] != ':' &&
        text[index] != '/' && text[index] != '?' && text[index] != '#') {
        if (!valid_host_character(text[index]) ||
            host_length + 1U >= arwill_http_host_capacity) {
            return arwill_http_invalid;
        }
        url->host[host_length++] = text[index++];
    }
    if (host_length == 0U || url->host[0] == '.' ||
        url->host[host_length - 1U] == '.') {
        return arwill_http_invalid;
    }
    url->host[host_length] = '\0';
    if (text[index] == ':') {
        index++;
        unsigned port = 0U;
        size_t digits = 0;
        while (text[index] >= '0' && text[index] <= '9') {
            port = port * 10U + (unsigned)(text[index] - '0');
            if (port > UINT16_MAX) {
                return arwill_http_invalid;
            }
            index++;
            digits++;
        }
        if (digits == 0U || port == 0U) {
            return arwill_http_invalid;
        }
        url->port = (uint16_t)port;
    }

    if (text[index] == '#') {
        return arwill_http_invalid;
    }
    size_t path_length = 0;
    if (text[index] != '/') {
        url->path[path_length++] = '/';
    }
    while (text[index] != '\0' && text[index] != '#') {
        const unsigned char value = (unsigned char)text[index++];
        if (value <= 0x20U || value >= 0x7fU ||
            path_length + 1U >= arwill_http_path_capacity) {
            return arwill_http_invalid;
        }
        url->path[path_length++] = (char)value;
    }
    if (text[index] == '#') {
        index++;
        while (text[index] != '\0') {
            index++;
        }
    }
    url->path[path_length] = '\0';
    return arwill_http_ok;
}

int arwill_http_build_request(
    enum arwill_http_method method,
    const struct arwill_http_url *url,
    const uint8_t *body,
    size_t body_length,
    uint8_t *output,
    size_t capacity,
    size_t *length
) {
    if (url == 0 || output == 0 || length == 0 ||
        url->host[0] == '\0' || url->path[0] == '\0' ||
        url->port == 0U || (body == 0 && body_length != 0U) ||
        (method != arwill_http_method_get &&
            method != arwill_http_method_post)) {
        return arwill_http_invalid;
    }

    size_t used = 0;
    const char *method_text =
        method == arwill_http_method_get ? "GET " : "POST ";
    if (!append_text(output, capacity, &used, method_text) ||
        !append_text(output, capacity, &used, url->path) ||
        !append_text(output, capacity, &used, " HTTP/1.0\r\nHost: ") ||
        !append_text(output, capacity, &used, url->host)) {
        return arwill_http_too_large;
    }
    const uint16_t default_port =
        url->scheme == arwill_http_scheme_https ? 443U : 80U;
    if (url->port != default_port &&
        (!append_byte(output, capacity, &used, (uint8_t)':') ||
            !append_decimal(output, capacity, &used, url->port))) {
        return arwill_http_too_large;
    }
    if (!append_text(output, capacity, &used,
            "\r\nConnection: close\r\nUser-Agent: arwill-curl/0\r\n")) {
        return arwill_http_too_large;
    }
    if (method == arwill_http_method_post &&
        (!append_text(output, capacity, &used,
            "Content-Type: application/octet-stream\r\nContent-Length: ") ||
            !append_decimal(output, capacity, &used, body_length) ||
            !append_text(output, capacity, &used, "\r\n"))) {
        return arwill_http_too_large;
    }
    if (!append_text(output, capacity, &used, "\r\n")) {
        return arwill_http_too_large;
    }
    for (size_t index = 0; index < body_length; index++) {
        if (!append_byte(output, capacity, &used, body[index])) {
            return arwill_http_too_large;
        }
    }
    *length = used;
    return arwill_http_ok;
}
