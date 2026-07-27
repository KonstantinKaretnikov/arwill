#include <stddef.h>
#include <stdint.h>

#include <arwill/user/dns.h>
#include <arwill/user/http.h>
#include <arwill/user/tls.h>

enum {
    syscall_write = 1,
    syscall_read = 3,
    syscall_clock = 4,
    syscall_net_open = 8,
    syscall_net_bind = 9,
    syscall_net_connect = 11,
    syscall_net_read = 13,
    syscall_net_write = 14,
    syscall_net_close = 15,
    syscall_udp_open = 16,
    syscall_udp_bind = 17,
    syscall_udp_connect = 18,
    syscall_udp_send = 19,
    syscall_udp_receive = 20,
    syscall_udp_close = 21,
    syscall_random_fill = 22,
    syscall_realtime = 23,
    net_retry = -1,
    net_closed = -4,
    qemu_dns_server_address = 0x0a000203U,
    public_dns_server_address = 0x01010101U,
    dns_port = 53,
    dns_local_port = 49152,
    http_local_port = 49153,
    input_capacity = 256,
    request_capacity = 768,
    response_header_capacity = 1024
};

static long call0(unsigned long number) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

static long call1(unsigned long number, unsigned long first) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) :
        "a"(number), "D"(first) : "memory");
    return result;
}

static long call2(unsigned long number, unsigned long first,
    unsigned long second) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) :
        "a"(number), "D"(first), "S"(second) : "memory");
    return result;
}

static long call3(unsigned long number, unsigned long first,
    const void *second, size_t third) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) :
        "a"(number), "D"(first), "S"(second), "d"(third) : "memory");
    return result;
}

static size_t text_length(const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static void console_write(const char *text) {
    (void)call2(syscall_write, (unsigned long)text, text_length(text));
}

static size_t read_line(const char *prompt, char *buffer, size_t capacity) {
    static const char erase[] = "\b \b";
    console_write(prompt);
    size_t length = 0U;
    while (length + 1U < capacity) {
        char value = '\0';
        if (call2(syscall_read, (unsigned long)&value, 1U) != 1L ||
            value == 0x03) {
            return 0U;
        }
        if (value == '\n' || value == '\r') {
            console_write("\n");
            break;
        }
        if (value == 0x08 || value == 0x7f) {
            if (length != 0U) {
                length--;
                (void)call2(syscall_write, (unsigned long)erase,
                    sizeof(erase) - 1U);
            }
            continue;
        }
        buffer[length++] = value;
        (void)call2(syscall_write, (unsigned long)&value, 1U);
    }
    buffer[length] = '\0';
    return length;
}

static int text_equals(const char *left, const char *right) {
    size_t index = 0U;
    while (left[index] != '\0' && right[index] != '\0' &&
        left[index] == right[index]) {
        index++;
    }
    return left[index] == right[index];
}

static int parse_ipv4(const char *text, uint32_t *address) {
    uint32_t result = 0U;
    size_t index = 0U;
    for (size_t part = 0U; part < 4U; part++) {
        unsigned value = 0U;
        size_t digits = 0U;
        while (text[index] >= '0' && text[index] <= '9') {
            value = value * 10U + (unsigned)(text[index] - '0');
            index++;
            digits++;
        }
        if (digits == 0U || value > 255U ||
            (part != 3U && text[index++] != '.')) {
            return 0;
        }
        result = (result << 8U) | value;
    }
    if (text[index] != '\0') {
        return 0;
    }
    *address = result;
    return 1;
}

static int resolve_via_server(
    const char *host,
    uint32_t server_address,
    uint32_t *address
) {
    uint8_t message[input_capacity];
    size_t message_length = 0U;
    const uint16_t identifier = (uint16_t)call0(syscall_clock);
    if (arwill_dns_build_a_query(host, identifier, message,
            sizeof(message), &message_length) != arwill_dns_ok ||
        call0(syscall_udp_open) != 0L ||
        call1(syscall_udp_bind, dns_local_port) != 0L) {
        return 0;
    }
    long result;
    unsigned long deadline = (unsigned long)call0(syscall_clock) + 5000UL;
    do {
        result = call2(syscall_udp_connect, server_address, dns_port);
    } while (result == net_retry &&
        (unsigned long)call0(syscall_clock) < deadline);
    if (result != 0L) {
        (void)call0(syscall_udp_close);
        return 0;
    }
    do {
        result = call2(syscall_udp_send,
            (unsigned long)message, message_length);
    } while (result == net_retry);
    if (result != (long)message_length) {
        (void)call0(syscall_udp_close);
        return 0;
    }
    deadline = (unsigned long)call0(syscall_clock) + 5000UL;
    do {
        result = call2(syscall_udp_receive,
            (unsigned long)message, sizeof(message));
    } while (result == net_retry &&
        (unsigned long)call0(syscall_clock) < deadline);
    (void)call0(syscall_udp_close);
    if (result <= 0L) {
        return 0;
    }
    return arwill_dns_parse_a_response(
        message, (size_t)result, identifier, address) == arwill_dns_ok;
}

static int resolve_host(const char *host, uint32_t *address) {
    if (parse_ipv4(host, address)) {
        return 1;
    }
    return resolve_via_server(host, qemu_dns_server_address, address) ||
        resolve_via_server(host, public_dns_server_address, address);
}

static int open_connection(uint32_t address, uint16_t port) {
    const long handle = call0(syscall_net_open);
    if (handle < 0L ||
        call2(syscall_net_bind, (unsigned long)handle,
            http_local_port) != 0L) {
        return -1;
    }
    long result;
    do {
        result = call3(syscall_net_connect, (unsigned long)handle,
            (const void *)(uintptr_t)address, port);
    } while (result == net_retry);
    if (result != 0L) {
        (void)call1(syscall_net_close, (unsigned long)handle);
        return -1;
    }
    return (int)handle;
}

static int write_plain(int handle, const uint8_t *request, size_t length) {
    long result;
    size_t offset = 0U;
    while (offset < length) {
        size_t chunk = length - offset;
        if (chunk > input_capacity) {
            chunk = input_capacity;
        }
        do {
            result = call3(syscall_net_write, (unsigned long)handle,
                request + offset, chunk);
        } while (result == net_retry);
        if (result <= 0L) {
            return 0;
        }
        offset += (size_t)result;
    }
    return 1;
}

struct response_state {
    uint8_t ending[4];
    size_t ending_count;
    uint8_t header[response_header_capacity];
    size_t header_length;
    size_t content_length;
    size_t body_received;
    int has_content_length;
    int complete;
    int body;
};

static int ascii_equal_fold(uint8_t left, char right) {
    if (left >= 'A' && left <= 'Z') {
        left = (uint8_t)(left + ('a' - 'A'));
    }
    if (right >= 'A' && right <= 'Z') {
        right = (char)(right + ('a' - 'A'));
    }
    return left == (uint8_t)right;
}

static void parse_content_length(struct response_state *state) {
    static const char name[] = "content-length:";
    for (size_t offset = 0U;
         offset + sizeof(name) - 1U < state->header_length;
         offset++) {
        if (offset != 0U && state->header[offset - 1U] != '\n') {
            continue;
        }
        size_t index = 0U;
        while (index + 1U < sizeof(name) &&
            ascii_equal_fold(state->header[offset + index], name[index])) {
            index++;
        }
        if (index + 1U != sizeof(name)) {
            continue;
        }
        size_t cursor = offset + index;
        while (cursor < state->header_length &&
            (state->header[cursor] == ' ' || state->header[cursor] == '\t')) {
            cursor++;
        }
        size_t value = 0U;
        size_t digits = 0U;
        while (cursor < state->header_length &&
            state->header[cursor] >= '0' && state->header[cursor] <= '9') {
            const size_t digit = (size_t)(state->header[cursor] - '0');
            if (value > (SIZE_MAX - digit) / 10U) {
                return;
            }
            value = value * 10U + digit;
            cursor++;
            digits++;
        }
        if (digits != 0U) {
            state->content_length = value;
            state->has_content_length = 1;
        }
        return;
    }
}

static int receive_response(
    void *context,
    const uint8_t *input,
    size_t length
) {
    struct response_state *state = context;
    for (size_t index = 0U; index < length; index++) {
        if (state->body) {
            size_t body_length = length - index;
            if (state->has_content_length &&
                body_length > state->content_length - state->body_received) {
                body_length = state->content_length - state->body_received;
            }
            if (body_length != 0U) {
                (void)call2(syscall_write, (unsigned long)&input[index],
                    body_length);
                state->body_received += body_length;
            }
            if (state->has_content_length &&
                state->body_received == state->content_length) {
                state->complete = 1;
            }
            break;
        }
        if (state->header_length < sizeof(state->header)) {
            state->header[state->header_length++] = input[index];
        }
        if (state->ending_count < sizeof(state->ending)) {
            state->ending[state->ending_count++] = input[index];
        } else {
            state->ending[0] = state->ending[1];
            state->ending[1] = state->ending[2];
            state->ending[2] = state->ending[3];
            state->ending[3] = input[index];
        }
        if (state->ending_count == sizeof(state->ending) &&
            state->ending[0] == '\r' && state->ending[1] == '\n' &&
            state->ending[2] == '\r' && state->ending[3] == '\n') {
                state->body = 1;
                parse_content_length(state);
                if (state->has_content_length &&
                    state->content_length == 0U) {
                    state->complete = 1;
                }
            }
        }
    return state->complete ? 2 : 1;
}

static int print_plain_response(int handle, struct response_state *state) {
    uint8_t input[input_capacity];
    unsigned long deadline = (unsigned long)call0(syscall_clock) + 5000UL;
    for (;;) {
        long result;
        do {
            result = call3(syscall_net_read, (unsigned long)handle,
                input, sizeof(input));
        } while (result == net_retry &&
            (unsigned long)call0(syscall_clock) < deadline);
        if (result == net_retry) {
            break;
        }
        if (result == net_closed) {
            break;
        }
        if (result <= 0L) {
            return 0;
        }
        deadline = (unsigned long)call0(syscall_clock) + 5000UL;
        (void)receive_response(state, input, (size_t)result);
        if (state->complete) {
            break;
        }
    }
    console_write("\n");
    return state->body &&
        (!state->has_content_length || state->complete);
}

struct tls_socket {
    int handle;
};

static int tls_socket_read(void *context, uint8_t *output, size_t capacity) {
    const struct tls_socket *socket = context;
    if (capacity > input_capacity) {
        capacity = input_capacity;
    }
    long result;
    const unsigned long deadline =
        (unsigned long)call0(syscall_clock) + 10000UL;
    do {
        result = call3(syscall_net_read, (unsigned long)socket->handle,
            output, capacity);
    } while (result == net_retry &&
        (unsigned long)call0(syscall_clock) < deadline);
    return result > 0L ? (int)result : -1;
}

static int tls_socket_write(
    void *context,
    const uint8_t *input,
    size_t length
) {
    const struct tls_socket *socket = context;
    if (length > input_capacity) {
        length = input_capacity;
    }
    long result;
    const unsigned long deadline =
        (unsigned long)call0(syscall_clock) + 10000UL;
    do {
        result = call3(syscall_net_write, (unsigned long)socket->handle,
            input, length);
    } while (result == net_retry &&
        (unsigned long)call0(syscall_clock) < deadline);
    return result > 0L ? (int)result : -1;
}

static int print_tls_response(
    int handle,
    const char *host,
    const uint8_t *request,
    size_t request_length,
    struct response_state *state
) {
    uint8_t entropy[32];
    const long unix_seconds = call0(syscall_realtime);
    if (unix_seconds < 0L ||
        call2(syscall_random_fill, (unsigned long)entropy,
            sizeof(entropy)) != (long)sizeof(entropy)) {
        console_write("curl: secure runtime unavailable\n");
        return 0;
    }
    struct tls_socket socket = { .handle = handle };
    const struct arwill_tls_transport transport = {
        .context = &socket,
        .read = tls_socket_read,
        .write = tls_socket_write
    };
    int tls_error = 0;
    const int result = arwill_tls_exchange(
        host, (uint64_t)unix_seconds, entropy, sizeof(entropy),
        &transport, request, request_length,
        receive_response, state, &tls_error
    );
    (void)tls_error;
    if (result != arwill_tls_ok) {
        console_write("curl: TLS handshake or certificate failed\n");
        return 0;
    }
    console_write("\n");
    return state->body &&
        (!state->has_content_length || state->complete);
}

int curl_main(void) {
    char method_text[8];
    char url_text[input_capacity];
    uint8_t body[input_capacity];
    uint8_t request[request_capacity];
    size_t body_length = 0U;
    enum arwill_http_method method;
    struct arwill_http_url url;
    size_t request_length = 0U;
    uint32_t address = 0U;

    console_write("Arwill curl: HTTP/HTTPS GET and POST\n");
    if (read_line("method [GET/POST]: ", method_text,
            sizeof(method_text)) == 0U) {
        return 1;
    }
    if (text_equals(method_text, "GET") || text_equals(method_text, "get")) {
        method = arwill_http_method_get;
    } else if (text_equals(method_text, "POST") ||
        text_equals(method_text, "post")) {
        method = arwill_http_method_post;
    } else {
        console_write("curl: unsupported method\n");
        return 2;
    }
    if (read_line("url: ", url_text, sizeof(url_text)) == 0U ||
        arwill_http_parse_url(url_text, &url) != arwill_http_ok) {
        console_write("curl: invalid or unsupported URL\n");
        return 2;
    }
    if (method == arwill_http_method_post) {
        body_length = read_line("body: ", (char *)body, sizeof(body));
    }
    if (arwill_http_build_request(method, &url, body, body_length,
            request, sizeof(request), &request_length) != arwill_http_ok) {
        console_write("curl: request too large\n");
        return 2;
    }
    console_write("resolving ");
    console_write(url.host);
    console_write("...\n");
    if (!resolve_host(url.host, &address)) {
        console_write("curl: DNS resolution failed\n");
        return 3;
    }
    const int handle = open_connection(address, url.port);
    if (handle < 0) {
        console_write("curl: connection failed\n");
        return 4;
    }
    struct response_state response = {
        .ending = { 0U, 0U, 0U, 0U },
        .ending_count = 0U,
        .header = { 0U },
        .header_length = 0U,
        .content_length = 0U,
        .body_received = 0U,
        .has_content_length = 0,
        .complete = 0,
        .body = 0
    };
    int response_ok = 0;
    if (url.scheme == arwill_http_scheme_https) {
        response_ok = print_tls_response(
            handle, url.host, request, request_length, &response);
    } else if (write_plain(handle, request, request_length)) {
        response_ok = print_plain_response(handle, &response);
    }
    (void)call1(syscall_net_close, (unsigned long)handle);
    if (!response_ok) {
        console_write("curl: invalid HTTP response\n");
        return 5;
    }
    return 0;
}
