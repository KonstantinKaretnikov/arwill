#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/config.h>
#include <arwill/kernel/filesystem.h>

enum {
    config_file_capacity = 1024,
    seen_version = 1 << 0,
    seen_remote_enabled = 1 << 1,
    seen_remote_port = 1 << 2,
    seen_remote_key = 1 << 3,
    seen_log_level = 1 << 4,
    seen_all = seen_version | seen_remote_enabled | seen_remote_port |
        seen_remote_key | seen_log_level
};

static const char config_path[] = "/owner/arwill.conf";

static size_t string_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static int string_equals(const char *left, const char *right) {
    size_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        index++;
    }
    return left[index] == right[index];
}

static int token_equals(
    const char *token,
    size_t length,
    const char *expected
) {
    size_t index = 0;
    while (index < length && expected[index] != '\0') {
        if (token[index] != expected[index]) {
            return 0;
        }
        index++;
    }
    return index == length && expected[index] == '\0';
}

static void set_defaults(struct arwill_config *config) {
    config->version = 1;
    config->remote_enabled = 1;
    config->remote_port = 23232U;
    for (size_t index = 0; index < arwill_config_remote_key_capacity; index++) {
        config->remote_key[index] = '\0';
    }
    config->log_level = arwill_config_log_info;
    config->loaded_from_file = 0;
    config->valid = 1;
}

void arwill_config_init(
    struct arwill_config *config,
    const struct arwill_filesystem *filesystem
) {
    if (config == 0) {
        return;
    }
    config->filesystem = filesystem;
    set_defaults(config);
}

static int parse_decimal(const char *text, size_t length, uint32_t *value) {
    uint32_t result = 0;
    if (length == 0U || value == 0) {
        return 0;
    }
    for (size_t index = 0; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }
        const uint32_t digit = (uint32_t)(text[index] - '0');
        if (result > (UINT32_MAX - digit) / 10U) {
            return 0;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return 1;
}

static int copy_remote_key(
    char destination[arwill_config_remote_key_capacity],
    const char *source,
    size_t length
) {
    if (length >= arwill_config_remote_key_capacity) {
        return 0;
    }
    for (size_t index = 0; index < arwill_config_remote_key_capacity; index++) {
        destination[index] = '\0';
    }
    for (size_t index = 0; index < length; index++) {
        if ((uint8_t)source[index] < 0x21U || (uint8_t)source[index] > 0x7eU ||
            source[index] == '=') {
            return 0;
        }
        destination[index] = source[index];
    }
    destination[length] = '\0';
    return 1;
}

static int parse_line(
    struct arwill_config *candidate,
    const char *line,
    size_t length,
    unsigned *seen
) {
    size_t separator = length;
    for (size_t index = 0; index < length; index++) {
        if (line[index] == '=') {
            if (separator != length) {
                return 0;
            }
            separator = index;
        }
    }
    if (separator == 0U || separator == length) {
        return 0;
    }
    const char *value = &line[separator + 1U];
    const size_t value_length = length - separator - 1U;
    unsigned field = 0;

    if (token_equals(line, separator, "config.version")) {
        uint32_t version = 0;
        field = seen_version;
        if (!parse_decimal(value, value_length, &version) || version != 1U) {
            return 0;
        }
        candidate->version = version;
    } else if (token_equals(line, separator, "remote.enabled")) {
        field = seen_remote_enabled;
        if (token_equals(value, value_length, "true")) {
            candidate->remote_enabled = 1;
        } else if (token_equals(value, value_length, "false")) {
            candidate->remote_enabled = 0;
        } else {
            return 0;
        }
    } else if (token_equals(line, separator, "remote.port")) {
        uint32_t port = 0;
        field = seen_remote_port;
        if (!parse_decimal(value, value_length, &port) || port == 0U || port > 65535U) {
            return 0;
        }
        candidate->remote_port = (uint16_t)port;
    } else if (token_equals(line, separator, "remote.key")) {
        field = seen_remote_key;
        if (!copy_remote_key(candidate->remote_key, value, value_length)) {
            return 0;
        }
    } else if (token_equals(line, separator, "log.level")) {
        field = seen_log_level;
        if (!token_equals(value, value_length, "info")) {
            return 0;
        }
        candidate->log_level = arwill_config_log_info;
    } else {
        return 0;
    }

    if ((*seen & field) != 0U) {
        return 0;
    }
    *seen |= field;
    return 1;
}

int arwill_config_load(struct arwill_config *config) {
    if (config == 0) {
        return 0;
    }
    struct arwill_fs_file file;
    if (!arwill_filesystem_read_file(config->filesystem, config_path, &file)) {
        set_defaults(config);
        return 1;
    }
    if (file.type != arwill_fs_file_text || file.contents == 0 ||
        file.size_bytes == 0U || file.size_bytes > config_file_capacity) {
        set_defaults(config);
        config->remote_enabled = 0;
        config->loaded_from_file = 1;
        config->valid = 0;
        return 0;
    }

    struct arwill_config candidate = *config;
    set_defaults(&candidate);
    candidate.filesystem = config->filesystem;
    unsigned seen = 0;
    size_t cursor = 0;
    while (cursor < file.size_bytes) {
        const size_t line_start = cursor;
        while (cursor < file.size_bytes && file.contents[cursor] != '\n' &&
               file.contents[cursor] != '\r') {
            cursor++;
        }
        if (cursor == line_start ||
            !parse_line(&candidate, &file.contents[line_start], cursor - line_start, &seen)) {
            set_defaults(config);
            config->remote_enabled = 0;
            config->loaded_from_file = 1;
            config->valid = 0;
            return 0;
        }
        if (cursor < file.size_bytes && file.contents[cursor] == '\r') {
            cursor++;
        }
        if (cursor < file.size_bytes && file.contents[cursor] == '\n') {
            cursor++;
        }
    }
    if (seen != seen_all) {
        set_defaults(config);
        config->remote_enabled = 0;
        config->loaded_from_file = 1;
        config->valid = 0;
        return 0;
    }
    candidate.loaded_from_file = 1;
    candidate.valid = 1;
    *config = candidate;
    return 1;
}

static int append_text(char *output, size_t capacity, size_t *offset, const char *text) {
    for (size_t index = 0; text[index] != '\0'; index++) {
        if (*offset >= capacity) {
            return 0;
        }
        output[*offset] = text[index];
        *offset = *offset + 1U;
    }
    return 1;
}

static int append_decimal(char *output, size_t capacity, size_t *offset, uint32_t value) {
    char reversed[10];
    size_t length = 0;
    do {
        reversed[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (length != 0U) {
        if (*offset >= capacity) {
            return 0;
        }
        length--;
        output[*offset] = reversed[length];
        *offset = *offset + 1U;
    }
    return 1;
}

static int persist_config(const struct arwill_config *config) {
    char output[256];
    size_t length = 0;
    if (!append_text(output, sizeof(output), &length, "config.version=1\n") ||
        !append_text(output, sizeof(output), &length, "remote.enabled=") ||
        !append_text(output, sizeof(output), &length,
            config->remote_enabled ? "true\n" : "false\n") ||
        !append_text(output, sizeof(output), &length, "remote.port=") ||
        !append_decimal(output, sizeof(output), &length, config->remote_port) ||
        !append_text(output, sizeof(output), &length, "\nremote.key=") ||
        !append_text(output, sizeof(output), &length, config->remote_key) ||
        !append_text(output, sizeof(output), &length, "\nlog.level=info\n")) {
        return 0;
    }
    return arwill_filesystem_write_bytes(
        config->filesystem,
        config_path,
        arwill_fs_file_text,
        (const uint8_t *)output,
        length
    );
}

int arwill_config_set(
    struct arwill_config *config,
    const char *key,
    const char *value
) {
    if (config == 0 || key == 0 || value == 0 || !config->valid) {
        return 0;
    }
    struct arwill_config candidate = *config;
    if (string_equals(key, "remote.enabled")) {
        if (string_equals(value, "true")) {
            candidate.remote_enabled = 1;
        } else if (string_equals(value, "false")) {
            candidate.remote_enabled = 0;
        } else {
            return 0;
        }
    } else if (string_equals(key, "remote.port")) {
        uint32_t port = 0;
        if (!parse_decimal(value, string_length(value), &port) ||
            port == 0U || port > 65535U) {
            return 0;
        }
        candidate.remote_port = (uint16_t)port;
    } else if (string_equals(key, "log.level")) {
        if (!string_equals(value, "info")) {
            return 0;
        }
        candidate.log_level = arwill_config_log_info;
    } else {
        return 0;
    }
    if (!persist_config(&candidate)) {
        return 0;
    }
    candidate.loaded_from_file = 1;
    *config = candidate;
    return 1;
}

int arwill_config_set_remote_key(
    struct arwill_config *config,
    const char *value
) {
    if (config == 0 || value == 0 || !config->valid) {
        return 0;
    }
    struct arwill_config candidate = *config;
    if (!copy_remote_key(candidate.remote_key, value, string_length(value)) ||
        !persist_config(&candidate)) {
        return 0;
    }
    candidate.loaded_from_file = 1;
    *config = candidate;
    return 1;
}

const char *arwill_config_log_level_name(enum arwill_config_log_level level) {
    return level == arwill_config_log_info ? "info" : "unknown";
}
