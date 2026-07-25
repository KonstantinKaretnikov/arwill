#include <stddef.h>
#include <stdint.h>

#include <arwill/user/dns.h>
#include <arwill/user/http.h>

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
    net_retry = -1,
    net_closed = -4,
    dns_server_address = 0x01010101U,
    dns_port = 53,
    dns_local_port = 49152,
    http_local_port = 49153,
    input_capacity = 256,
    request_capacity = 768
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

static int resolve_host(const char *host, uint32_t *address) {
    uint8_t message[input_capacity];
    size_t message_length = 0U;
    const uint16_t identifier = (uint16_t)call0(syscall_clock);
    if (parse_ipv4(host, address)) {
        return 1;
    }
    if (arwill_dns_build_a_query(host, identifier, message,
            sizeof(message), &message_length) != arwill_dns_ok ||
        call0(syscall_udp_open) != 0L ||
        call1(syscall_udp_bind, dns_local_port) != 0L) {
        return 0;
    }
    long result;
    unsigned long deadline = (unsigned long)call0(syscall_clock) + 5000UL;
    do {
        result = call2(syscall_udp_connect, dns_server_address, dns_port);
    } while (result == net_retry &&
        (unsigned long)call0(syscall_clock) < deadline);
    if (result != 0L) {
        console_write("curl: DNS connect timeout\n");
        (void)call0(syscall_udp_close);
        return 0;
    }
    do {
        result = call2(syscall_udp_send,
            (unsigned long)message, message_length);
    } while (result == net_retry);
    if (result != (long)message_length) {
        console_write("curl: DNS send failed\n");
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
        console_write("curl: DNS response timeout\n");
        return 0;
    }
    return arwill_dns_parse_a_response(
        message, (size_t)result, identifier, address) == arwill_dns_ok;
}

static int send_request(uint32_t address, uint16_t port,
    const uint8_t *request, size_t length) {
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
            return -1;
        }
        offset += (size_t)result;
    }
    return (int)handle;
}

static int print_response(int handle) {
    uint8_t input[input_capacity];
    uint8_t ending[4] = { 0U, 0U, 0U, 0U };
    size_t ending_count = 0U;
    int body = 0;
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
        for (size_t index = 0U; index < (size_t)result; index++) {
            if (body) {
                (void)call2(syscall_write, (unsigned long)&input[index],
                    (size_t)result - index);
                break;
            }
            if (ending_count < sizeof(ending)) {
                ending[ending_count++] = input[index];
            } else {
                ending[0] = ending[1];
                ending[1] = ending[2];
                ending[2] = ending[3];
                ending[3] = input[index];
            }
            if (ending_count == sizeof(ending) &&
                ending[0] == '\r' && ending[1] == '\n' &&
                ending[2] == '\r' && ending[3] == '\n') {
                body = 1;
            }
        }
    }
    (void)call1(syscall_net_close, (unsigned long)handle);
    console_write("\n");
    return body;
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

    console_write("Arwill curl: HTTP only\n");
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
    const int handle = send_request(address, url.port, request, request_length);
    if (handle < 0) {
        console_write("curl: connection failed\n");
        return 4;
    }
    if (!print_response(handle)) {
        console_write("curl: invalid HTTP response\n");
        return 5;
    }
    return 0;
}
