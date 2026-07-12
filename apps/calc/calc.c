typedef unsigned long size_t;

static long syscall_write(const char *text, size_t length) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(1UL), "D"(text), "S"(length) : "memory");
    return result;
}

static long syscall_read(char *buffer, size_t length) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(3UL), "D"(buffer), "S"(length) : "memory");
    return result;
}

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static long parse_number(const char *text, size_t *index, size_t length) {
    long value = 0;
    int negative = 0;

    if (*index < length && text[*index] == '-') {
        negative = 1;
        *index = *index + 1U;
    }

    while (*index < length && text[*index] >= '0' && text[*index] <= '9') {
        value = value * 10L + (long)(text[*index] - '0');
        *index = *index + 1U;
    }

    return negative ? -value : value;
}

static size_t format_number(char *output, long value) {
    char reversed[24];
    size_t digits = 0;
    const size_t prefix = value < 0 ? 1U : 0U;
    unsigned long magnitude;

    if (value < 0) {
        output[0] = '-';
        magnitude = (unsigned long)(-(value + 1L)) + 1UL;
    } else {
        magnitude = (unsigned long)value;
    }

    do {
        reversed[digits] = (char)('0' + (magnitude % 10UL));
        digits++;
        magnitude /= 10UL;
    } while (magnitude != 0UL);

    for (size_t index = 0; index < digits; index++) {
        output[prefix + index] = reversed[digits - index - 1U];
    }

    output[prefix + digits] = '\n';
    return prefix + digits + 1U;
}

static int calculate(const char *text, size_t length, long *result) {
    size_t index = 0;
    const long left = parse_number(text, &index, length);
    const char operation = index < length ? text[index++] : '\0';
    const long right = parse_number(text, &index, length);

    if (operation == '+') {
        *result = left + right;
    } else if (operation == '-') {
        *result = left - right;
    } else if (operation == '*') {
        *result = left * right;
    } else if (operation == '/' && right != 0) {
        *result = left / right;
    } else {
        return 0;
    }

    return 1;
}

int calculator_main(void) {
    static const char prompt[] = "calc> ";
    static const char equals[] = "=";
    static const char interrupted[] = "^C\n";
    static const char error[] = "error\n";
    static const char erase[] = "\b \b";
    char input[64];
    char output[32];

    for (;;) {
        syscall_write(prompt, text_length(prompt));
        size_t length = 0;

        while (length < sizeof(input) - 1U) {
            if (syscall_read(&input[length], 1U) != 1L) {
                return 1;
            }
            if (input[length] == 0x03) {
                syscall_write(interrupted, text_length(interrupted));
                return 130;
            }
            if (input[length] == 0x08 || input[length] == 0x7f) {
                if (length != 0U) {
                    length--;
                    syscall_write(erase, text_length(erase));
                }
                continue;
            }
            if (input[length] == '\n' || input[length] == '\r') {
                break;
            }
            syscall_write(&input[length], 1U);
            length++;
        }

        long result = 0;
        if (!calculate(input, length, &result)) {
            syscall_write(error, text_length(error));
            continue;
        }

        syscall_write(equals, text_length(equals));
        const size_t output_length = format_number(output, result);
        syscall_write(output, output_length);
    }
}
