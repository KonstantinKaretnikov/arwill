typedef unsigned long size_t;

enum {
    document_capacity = 2048,
    path_capacity = 64,
    screen_capacity = 3072,
    screen_columns = 78,
    screen_rows = 22
};

static long syscall_write(const char *text, size_t length) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(1UL), "D"(text), "S"(length)
        : "memory"
    );
    return result;
}

static long syscall_read(char *buffer, size_t length) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(3UL), "D"(buffer), "S"(length)
        : "memory"
    );
    return result;
}

static long syscall_read_file(
    const char *path,
    size_t path_length,
    char *buffer,
    size_t capacity
) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(5UL), "D"(path), "S"(path_length), "d"(buffer), "c"(capacity)
        : "memory"
    );
    return result;
}

static long syscall_write_file(
    const char *path,
    size_t path_length,
    const char *buffer,
    size_t length
) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(6UL), "D"(path), "S"(path_length), "d"(buffer), "c"(length)
        : "memory"
    );
    return result;
}

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static void terminal_write(const char *text, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        size_t chunk = length - offset;
        if (chunk > 256U) {
            chunk = 256U;
        }
        if (syscall_write(&text[offset], chunk) <= 0) {
            return;
        }
        offset += chunk;
    }
}

static void write_text(const char *text) {
    terminal_write(text, text_length(text));
}

static int read_byte(char *byte) {
    return syscall_read(byte, 1U) == 1L;
}

static int append_char(char *output, size_t *length, char value) {
    if (*length >= screen_capacity) {
        return 0;
    }
    output[*length] = value;
    *length = *length + 1U;
    return 1;
}

static void append_text(char *output, size_t *length, const char *text) {
    for (size_t index = 0; text[index] != '\0'; index++) {
        if (!append_char(output, length, text[index])) {
            return;
        }
    }
}

static void append_number(char *output, size_t *length, size_t value) {
    char reversed[24];
    size_t count = 0;
    do {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) {
        count--;
        (void)append_char(output, length, reversed[count]);
    }
}

static void visual_position(
    const char *document,
    size_t cursor,
    size_t *row,
    size_t *column
) {
    *row = 0;
    *column = 0;
    for (size_t index = 0; index < cursor; index++) {
        if (document[index] == '\n') {
            *row = *row + 1U;
            *column = 0;
        } else {
            *column = *column + 1U;
            if (*column >= screen_columns) {
                *row = *row + 1U;
                *column = 0;
            }
        }
    }
}

static void render(
    const char *document,
    size_t length,
    size_t cursor,
    const char *path,
    int dirty,
    const char *message
) {
    char output[screen_capacity];
    size_t output_length = 0;
    size_t cursor_row = 0;
    size_t cursor_column = 0;
    visual_position(document, cursor, &cursor_row, &cursor_column);
    const size_t scroll = cursor_row >= screen_rows
        ? cursor_row - screen_rows + 1U : 0U;

    append_text(output, &output_length, "\x1b[2J\x1b[H");
    size_t row = 0;
    size_t column = 0;
    for (size_t index = 0; index < length; index++) {
        const char value = document[index];
        if (value == '\n') {
            if (row >= scroll && row < scroll + screen_rows) {
                append_text(output, &output_length, "\r\n");
            }
            row++;
            column = 0;
            continue;
        }
        if (row >= scroll && row < scroll + screen_rows) {
            (void)append_char(output, &output_length, value);
        }
        column++;
        if (column >= screen_columns) {
            if (row >= scroll && row < scroll + screen_rows) {
                append_text(output, &output_length, "\r\n");
            }
            row++;
            column = 0;
        }
    }

    append_text(output, &output_length, "\x1b[24;1H\x1b[7m ");
    append_text(output, &output_length, path);
    append_text(output, &output_length, dirty ? "  modified  " : "  saved  ");
    append_text(output, &output_length, message);
    append_text(output, &output_length, "  ^S save  ^Q quit \x1b[0m");
    append_text(output, &output_length, "\x1b[");
    append_number(output, &output_length, cursor_row - scroll + 1U);
    (void)append_char(output, &output_length, ';');
    append_number(output, &output_length, cursor_column + 1U);
    (void)append_char(output, &output_length, 'H');
    terminal_write(output, output_length);
}

static size_t line_start(const char *document, size_t cursor) {
    while (cursor != 0U && document[cursor - 1U] != '\n') {
        cursor--;
    }
    return cursor;
}

static size_t line_end(const char *document, size_t length, size_t cursor) {
    while (cursor < length && document[cursor] != '\n') {
        cursor++;
    }
    return cursor;
}

static size_t move_up(const char *document, size_t cursor) {
    const size_t start = line_start(document, cursor);
    if (start == 0U) {
        return cursor;
    }
    const size_t column = cursor - start;
    const size_t previous_end = start - 1U;
    const size_t previous_start = line_start(document, previous_end);
    const size_t previous_length = previous_end - previous_start;
    return previous_start + (column < previous_length ? column : previous_length);
}

static size_t move_down(const char *document, size_t length, size_t cursor) {
    const size_t start = line_start(document, cursor);
    const size_t end = line_end(document, length, cursor);
    if (end >= length) {
        return cursor;
    }
    const size_t column = cursor - start;
    const size_t next_start = end + 1U;
    const size_t next_end = line_end(document, length, next_start);
    const size_t next_length = next_end - next_start;
    return next_start + (column < next_length ? column : next_length);
}

static void remove_byte(char *document, size_t *length, size_t index) {
    for (size_t cursor = index; cursor + 1U < *length; cursor++) {
        document[cursor] = document[cursor + 1U];
    }
    *length = *length - 1U;
}

static int insert_byte(
    char *document,
    size_t *length,
    size_t *cursor,
    char value
) {
    if (*length >= document_capacity) {
        return 0;
    }
    for (size_t index = *length; index > *cursor; index--) {
        document[index] = document[index - 1U];
    }
    document[*cursor] = value;
    *cursor = *cursor + 1U;
    *length = *length + 1U;
    return 1;
}

static int read_path(char *path, size_t *length) {
    write_text("edit file: ");
    *length = 0;
    for (;;) {
        char byte = 0;
        if (!read_byte(&byte)) {
            return 0;
        }
        if (byte == 0x03) {
            write_text("^C\n");
            return 0;
        }
        if (byte == '\r' || byte == '\n') {
            write_text("\n");
            return *length != 0U;
        }
        if (byte == 0x08 || byte == 0x7f) {
            if (*length != 0U) {
                *length = *length - 1U;
                write_text("\b \b");
            }
            continue;
        }
        if (byte >= 0x20 && byte <= 0x7e && *length + 1U < path_capacity) {
            path[*length] = byte;
            *length = *length + 1U;
            terminal_write(&byte, 1U);
        }
    }
}

int editor_main(void) {
    char document[document_capacity];
    char path[path_capacity];
    size_t path_length = 0;
    if (!read_path(path, &path_length)) {
        return 130;
    }
    path[path_length] = '\0';

    const long loaded = syscall_read_file(
        path, path_length, document, document_capacity
    );
    size_t length = loaded < 0 ? 0U : (size_t)loaded;
    size_t cursor = 0;
    int dirty = 0;
    int quit_armed = 0;
    const char *message = loaded < 0 ? "new file" : "loaded";

    for (;;) {
        render(document, length, cursor, path, dirty, message);
        message = "";
        char byte = 0;
        if (!read_byte(&byte)) {
            return 1;
        }
        if (byte == 0x03) {
            write_text("\x1b[2J\x1b[H^C\n");
            return 130;
        }
        if (byte == 0x13) {
            const long saved = syscall_write_file(path, path_length, document, length);
            if (saved == (long)length) {
                dirty = 0;
                quit_armed = 0;
                message = "saved";
            } else {
                message = "save failed";
            }
            continue;
        }
        if (byte == 0x11) {
            if (dirty && !quit_armed) {
                quit_armed = 1;
                message = "unsaved; ^Q again";
                continue;
            }
            write_text("\x1b[2J\x1b[H");
            return 0;
        }
        quit_armed = 0;

        if (byte == 0x1b) {
            char bracket = 0;
            char code = 0;
            if (!read_byte(&bracket) || bracket != '[' || !read_byte(&code)) {
                continue;
            }
            if (code == 'A') {
                cursor = move_up(document, cursor);
            } else if (code == 'B') {
                cursor = move_down(document, length, cursor);
            } else if (code == 'C' && cursor < length) {
                cursor++;
            } else if (code == 'D' && cursor != 0U) {
                cursor--;
            } else if (code == 'H') {
                cursor = line_start(document, cursor);
            } else if (code == 'F') {
                cursor = line_end(document, length, cursor);
            } else if (code == '3') {
                char tilde = 0;
                if (read_byte(&tilde) && tilde == '~' && cursor < length) {
                    remove_byte(document, &length, cursor);
                    dirty = 1;
                }
            }
            continue;
        }

        if (byte == 0x08 || byte == 0x7f) {
            if (cursor != 0U) {
                cursor--;
                remove_byte(document, &length, cursor);
                dirty = 1;
            }
            continue;
        }
        if (byte == '\r' || byte == '\n') {
            if (insert_byte(document, &length, &cursor, '\n')) {
                dirty = 1;
            } else {
                message = "file full";
            }
            continue;
        }
        if (byte >= 0x20 && byte <= 0x7e) {
            if (insert_byte(document, &length, &cursor, byte)) {
                dirty = 1;
            } else {
                message = "file full";
            }
        }
    }
}
