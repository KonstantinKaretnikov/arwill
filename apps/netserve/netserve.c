typedef unsigned long size_t;

enum {
    syscall_write = 1,
    syscall_net_open = 8,
    syscall_net_bind = 9,
    syscall_net_listen = 10,
    syscall_net_accept = 12,
    syscall_net_read = 13,
    syscall_net_write = 14,
    syscall_net_close = 15,
    net_retry = -1,
    service_port = 23233
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

int netserve_main(void) {
    static const char waiting[] = "netserve: listening on 23233\n";
    static const char response[] = "Arwill AWP TCP service\n";
    static const char failed[] = "netserve: failed\n";
    static const char complete[] = "netserve: served one connection\n";
    char request[64];

    const long handle = call0(syscall_net_open);
    if (handle < 0 || call2(syscall_net_bind, (unsigned long)handle,
            service_port) != 0L ||
        call1(syscall_net_listen, (unsigned long)handle) != 0L) {
        console_write(failed);
        return 1;
    }
    console_write(waiting);
    while (call1(syscall_net_accept, (unsigned long)handle) == net_retry) {
    }
    long read;
    do {
        read = call3(syscall_net_read, (unsigned long)handle,
            request, sizeof(request));
    } while (read == net_retry);
    if (read <= 0L || call3(syscall_net_write, (unsigned long)handle,
            response, sizeof(response) - 1U) !=
            (long)(sizeof(response) - 1U)) {
        console_write(failed);
        return 2;
    }
    while (call1(syscall_net_close, (unsigned long)handle) == net_retry) {
    }
    console_write(complete);
    return 0;
}
