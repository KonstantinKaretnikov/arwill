#include <stddef.h>
#include <stdint.h>

#include <arwill/identity.h>
#include <arwill/kernel/block_device.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/config.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/device.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/log.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/pci.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/scheduler.h>
#include <arwill/kernel/service.h>
#include <arwill/kernel/shell.h>
#include <arwill/kernel/tcp.h>
#include <arwill/kernel/tcp_stream.h>
#include <arwill/kernel/text.h>
#include <arwill/kernel/user.h>

enum {
    shell_line_capacity = 96,
    shell_path_capacity = 96,
    shell_history_capacity = 8,
    ascii_interrupt = 0x03,
    ascii_backspace = 0x08,
    ascii_escape = 0x1b,
    ascii_tab = '\t',
    ascii_delete = 0x7f,
    ascii_carriage_return = '\r',
    ascii_line_feed = '\n',
    ascii_left_bracket = '[',
    ascii_arrow_up = 'A',
    ascii_arrow_down = 'B',
    ascii_arrow_right = 'C',
    ascii_arrow_left = 'D',
    utf8_cyrillic_lead_0 = 0xd0,
    utf8_cyrillic_lead_1 = 0xd1,
    remote_authentication_max_attempts = 3,
    remote_authentication_timeout_ms = 30000,
    top_refresh_interval_ms = 1000,
};

enum shell_completion_kind {
    shell_completion_none,
    shell_completion_path,
    shell_completion_exec,
    shell_completion_directory_path,
    shell_completion_process,
    shell_completion_system,
    shell_completion_devices,
    shell_completion_network
};

struct shell_command {
    const char *name;
    enum shell_completion_kind completion;
};

/* User-visible help and completion table; internal smoke commands dispatch below. */
static const struct shell_command shell_commands[] = {
    { .name = "help", .completion = shell_completion_none },
    { .name = "version", .completion = shell_completion_none },
    { .name = "system", .completion = shell_completion_system },
    { .name = "devices", .completion = shell_completion_devices },
    { .name = "network", .completion = shell_completion_network },
    { .name = "top", .completion = shell_completion_none },
    { .name = "pwd", .completion = shell_completion_none },
    { .name = "cd", .completion = shell_completion_directory_path },
    { .name = "clear", .completion = shell_completion_none },
    { .name = "ls", .completion = shell_completion_path },
    { .name = "cat", .completion = shell_completion_path },
    { .name = "mkdir", .completion = shell_completion_directory_path },
    { .name = "rm", .completion = shell_completion_path },
    { .name = "stat", .completion = shell_completion_path },
    { .name = "config", .completion = shell_completion_none },
    { .name = "logs", .completion = shell_completion_none },
    { .name = "service", .completion = shell_completion_none },
    { .name = "ps", .completion = shell_completion_none },
    { .name = "run", .completion = shell_completion_process },
    { .name = "exec", .completion = shell_completion_exec },
    { .name = "exit", .completion = shell_completion_none },
    { .name = "halt", .completion = shell_completion_none },
};

static const char *const system_completions[] = {
    "memory", "storage", "interrupts", "scheduler", "runtime", "owner"
};

static const char *const device_completions[] = {
    "pci", "disk0", "net0"
};

static const char *const network_completions[] = {
    "ping", "tcp"
};

struct shell_history {
    char entries[shell_history_capacity][shell_line_capacity];
    size_t count;
};

enum shell_escape_state {
    shell_escape_none,
    shell_escape_started,
    shell_escape_bracket
};

enum shell_utf8_state {
    shell_utf8_none,
    shell_utf8_cyrillic_continuation
};

struct shell_input_normalizer {
    enum shell_utf8_state utf8_state;
    uint8_t utf8_lead;
    int russian_layout_active;
};

struct shell_process_context {
    const struct arwill_console *console;
    const struct arwill_user_runtime *user_runtime;
};

struct shell_environment {
    const struct arwill_filesystem *filesystem;
    struct arwill_memory *memory;
    const struct arwill_power *power;
    struct arwill_process_manager *processes;
    const struct arwill_pci_bus *pci;
    const struct arwill_network_device *network;
    struct arwill_ipv4_stack *ipv4;
    struct arwill_tcp_stream *remote_stream;
    const struct arwill_block_device *block_device;
    const struct arwill_interrupts *interrupts;
    const struct arwill_clock *clock;
    const struct arwill_user_runtime *user_runtime;
    const struct arwill_device_registry *devices;
    struct arwill_config *config;
    struct arwill_event_log *log;
    struct arwill_service_manager *services;
};

struct shell_session {
    const struct arwill_console *console;
    char line[shell_line_capacity];
    char current_directory[shell_path_capacity];
    size_t length;
    size_t cursor;
    struct shell_history history;
    size_t history_position;
    enum shell_escape_state escape_state;
    struct shell_input_normalizer normalizer;
    struct shell_process_context process_context;
    int remote;
    int active;
    int ignore_line_feed;
    uint32_t foreground_pid;
    int config_key_pending;
    char config_key[arwill_config_remote_key_capacity];
    size_t config_key_length;
    int authenticated;
    unsigned authentication_attempts;
    uint64_t authentication_started_milliseconds;
    char authentication_key[arwill_config_remote_key_capacity];
    size_t authentication_key_length;
    uint32_t tcp_timeouts_at_connection;
    int top_active;
    uint64_t top_last_refresh_milliseconds;
};

struct shell_builtin_process {
    const char *name;
    arwill_process_entry entry;
};

static void remote_console_write(void *context, const char *text) {
    struct arwill_tcp_stream *stream = (struct arwill_tcp_stream *)context;
    uint8_t chunk[128];
    size_t chunk_length = 0;

    for (size_t index = 0; text[index] != '\0'; index++) {
        if (text[index] == '\n') {
            if (chunk_length == sizeof(chunk)) {
                (void)arwill_tcp_stream_write(stream, chunk, chunk_length);
                chunk_length = 0;
            }
            chunk[chunk_length++] = '\r';
        }
        if (chunk_length == sizeof(chunk)) {
            (void)arwill_tcp_stream_write(stream, chunk, chunk_length);
            chunk_length = 0;
        }
        chunk[chunk_length++] = (uint8_t)text[index];
    }
    if (chunk_length != 0U) {
        (void)arwill_tcp_stream_write(stream, chunk, chunk_length);
    }
}

static size_t common_prefix_length(const char *left, const char *right, size_t limit) {
    size_t length = 0;

    while (length < limit && left[length] != '\0' && left[length] == right[length]) {
        length++;
    }

    return length;
}

static int is_printable_ascii(uint8_t byte) {
    return byte >= 0x20 && byte <= 0x7e;
}

static char ascii_to_uppercase(char value) {
    if (value >= 'a' && value <= 'z') {
        return (char)(value - 'a' + 'A');
    }

    return value;
}

static int map_russian_layout_codepoint(uint32_t codepoint, char *mapped) {
    int uppercase = 0;

    if (codepoint >= 0x0410U && codepoint <= 0x042fU) {
        codepoint += 0x20U;
        uppercase = 1;
    } else if (codepoint == 0x0401U) {
        codepoint = 0x0451U;
        uppercase = 1;
    }

    switch (codepoint) {
        case 0x0439U:
            *mapped = 'q';
            break;
        case 0x0446U:
            *mapped = 'w';
            break;
        case 0x0443U:
            *mapped = 'e';
            break;
        case 0x043aU:
            *mapped = 'r';
            break;
        case 0x0435U:
            *mapped = 't';
            break;
        case 0x043dU:
            *mapped = 'y';
            break;
        case 0x0433U:
            *mapped = 'u';
            break;
        case 0x0448U:
            *mapped = 'i';
            break;
        case 0x0449U:
            *mapped = 'o';
            break;
        case 0x0437U:
            *mapped = 'p';
            break;
        case 0x0445U:
            *mapped = '[';
            break;
        case 0x044aU:
            *mapped = ']';
            break;
        case 0x0444U:
            *mapped = 'a';
            break;
        case 0x044bU:
            *mapped = 's';
            break;
        case 0x0432U:
            *mapped = 'd';
            break;
        case 0x0430U:
            *mapped = 'f';
            break;
        case 0x043fU:
            *mapped = 'g';
            break;
        case 0x0440U:
            *mapped = 'h';
            break;
        case 0x043eU:
            *mapped = 'j';
            break;
        case 0x043bU:
            *mapped = 'k';
            break;
        case 0x0434U:
            *mapped = 'l';
            break;
        case 0x0436U:
            *mapped = ';';
            break;
        case 0x044dU:
            *mapped = '\'';
            break;
        case 0x044fU:
            *mapped = 'z';
            break;
        case 0x0447U:
            *mapped = 'x';
            break;
        case 0x0441U:
            *mapped = 'c';
            break;
        case 0x043cU:
            *mapped = 'v';
            break;
        case 0x0438U:
            *mapped = 'b';
            break;
        case 0x0442U:
            *mapped = 'n';
            break;
        case 0x044cU:
            *mapped = 'm';
            break;
        case 0x0431U:
            *mapped = ',';
            break;
        case 0x044eU:
            *mapped = '.';
            break;
        case 0x0451U:
            *mapped = '`';
            break;
        default:
            return 0;
    }

    if (uppercase) {
        *mapped = ascii_to_uppercase(*mapped);
    }

    return 1;
}

static uint32_t decode_two_byte_utf8(uint8_t lead, uint8_t continuation) {
    return (((uint32_t)lead & 0x1fU) << 6U) | ((uint32_t)continuation & 0x3fU);
}

static char normalize_russian_layout_ascii(
    const struct shell_input_normalizer *normalizer,
    uint8_t byte
) {
    if (normalizer->russian_layout_active && byte == '.') {
        return '/';
    }

    return (char)byte;
}

static int normalize_text_input_byte(
    struct shell_input_normalizer *normalizer,
    uint8_t byte,
    char *value
) {
    if (normalizer->utf8_state == shell_utf8_cyrillic_continuation) {
        normalizer->utf8_state = shell_utf8_none;

        if ((byte & 0xc0U) != 0x80U) {
            return 0;
        }

        const uint32_t codepoint = decode_two_byte_utf8(normalizer->utf8_lead, byte);

        if (!map_russian_layout_codepoint(codepoint, value)) {
            return 0;
        }

        normalizer->russian_layout_active = 1;
        return 1;
    }

    if (byte == utf8_cyrillic_lead_0 || byte == utf8_cyrillic_lead_1) {
        normalizer->utf8_lead = byte;
        normalizer->utf8_state = shell_utf8_cyrillic_continuation;
        return 0;
    }

    if (!is_printable_ascii(byte)) {
        return 0;
    }

    *value = normalize_russian_layout_ascii(normalizer, byte);
    return 1;
}

static void write_byte_echo(const struct arwill_console *console, uint8_t byte) {
    const char text[2] = { (char)byte, '\0' };

    arwill_console_write(console, text);
}

static void move_console_cursor_left(
    const struct arwill_console *console,
    size_t count
) {
    for (size_t index = 0; index < count; index++) {
        arwill_console_write(console, "\033[D");
    }
}

static void move_console_cursor_right(
    const struct arwill_console *console,
    size_t count
) {
    for (size_t index = 0; index < count; index++) {
        arwill_console_write(console, "\033[C");
    }
}

static int append_char_to_line(
    const struct arwill_console *console,
    char *line,
    size_t *length,
    char value
) {
    if (*length >= shell_line_capacity - 1U) {
        return 0;
    }

    line[*length] = value;
    *length = *length + 1U;
    write_byte_echo(console, (uint8_t)value);
    return 1;
}

static int insert_char_into_line(
    const struct arwill_console *console,
    char *line,
    size_t *length,
    size_t *cursor,
    char value
) {
    if (*length >= shell_line_capacity - 1U || *cursor > *length) {
        return 0;
    }

    for (size_t index = *length; index > *cursor; index--) {
        line[index] = line[index - 1U];
    }
    line[*cursor] = value;
    *length = *length + 1U;
    *cursor = *cursor + 1U;
    line[*length] = '\0';

    for (size_t index = *cursor - 1U; index < *length; index++) {
        write_byte_echo(console, (uint8_t)line[index]);
    }
    move_console_cursor_left(console, *length - *cursor);
    return 1;
}

static void write_uint64_decimal(const struct arwill_console *console, uint64_t value) {
    char reversed[20];
    char text[21];
    size_t length = 0;

    do {
        const uint64_t digit = value % 10U;
        reversed[length] = (char)('0' + digit);
        length++;
        value = value / 10U;
    } while (value != 0U && length < sizeof(reversed));

    for (size_t index = 0; index < length; index++) {
        text[index] = reversed[length - index - 1U];
    }

    text[length] = '\0';
    arwill_console_write(console, text);
}

static void write_size_decimal(const struct arwill_console *console, size_t value) {
    write_uint64_decimal(console, (uint64_t)value);
}

static void write_uint64_hex(const struct arwill_console *console, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[19];
    unsigned shift = 60;

    text[0] = '0';
    text[1] = 'x';

    for (size_t index = 0; index < 16U; index++) {
        text[index + 2U] = digits[(value >> shift) & 0xfU];
        if (shift >= 4U) {
            shift -= 4U;
        }
    }

    text[18] = '\0';
    arwill_console_write(console, text);
}

static void write_uint8_hex(const struct arwill_console *console, uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[3];

    text[0] = digits[(value >> 4U) & 0xfU];
    text[1] = digits[value & 0xfU];
    text[2] = '\0';
    arwill_console_write(console, text);
}

static void write_ipv4_address(
    const struct arwill_console *console,
    const uint8_t address[4]
) {
    for (size_t index = 0; index < 4U; index++) {
        if (index != 0U) {
            arwill_console_write(console, ".");
        }
        write_uint64_decimal(console, address[index]);
    }
}

static void print_remote_console_info(
    const struct arwill_console *console,
    const struct arwill_ipv4_stack *ipv4
) {
    struct arwill_tcp_endpoint_snapshot remote;
    if (!arwill_ipv4_tcp_endpoint_snapshot(ipv4, 0U, &remote)) {
        arwill_console_write_line(console, "remote console: unavailable");
        return;
    }
    arwill_console_write(console, "remote console: plaintext, connections ");
    write_uint64_decimal(console, remote.connections);
    arwill_console_write(console, ", disconnects ");
    write_uint64_decimal(console, remote.disconnects);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "remote bytes: received ");
    write_uint64_decimal(console, remote.bytes_received);
    arwill_console_write(console, ", sent ");
    write_uint64_decimal(console, remote.bytes_sent);
    arwill_console_write(console, ", dropped ");
    write_uint64_decimal(console, remote.bytes_dropped);
    arwill_console_write(console, ", send failures ");
    write_uint64_decimal(console, remote.send_failures);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp integrity: checksum drops ");
    write_uint64_decimal(console, ipv4->tcp_checksum_drops);
    arwill_console_write(console, ", duplicate acks ");
    write_uint64_decimal(console, ipv4->tcp_duplicate_acks);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp reliability: retransmissions ");
    write_uint64_decimal(console, ipv4->tcp_retransmissions);
    arwill_console_write(console, ", timeouts ");
    write_uint64_decimal(console, ipv4->tcp_timeouts);
    arwill_console_write(console, ", pending segments ");
    write_size_decimal(console, remote.pending_segments);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp timing: srtt ");
    write_uint64_decimal(console, remote.smoothed_round_trip_ms);
    arwill_console_write(console, " ms, rto ");
    write_uint64_decimal(console, remote.retransmission_timeout_ms);
    arwill_console_write(console, " ms, backoffs ");
    write_uint64_decimal(console, ipv4->tcp_retransmission_backoffs);
    arwill_console_write_line(console, "");
}

static uint64_t saturating_add_uint64(uint64_t left, uint64_t right) {
    if (left > UINT64_MAX - right) {
        return UINT64_MAX;
    }

    return left + right;
}

static void erase_line_contents(const struct arwill_console *console, size_t length) {
    for (size_t index = 0; index < length; index++) {
        arwill_console_write(console, "\b \b");
    }
}

static void replace_line(
    const struct arwill_console *console,
    char *line,
    size_t *length,
    size_t *cursor,
    const char *replacement
) {
    if (*cursor < *length) {
        move_console_cursor_right(console, *length - *cursor);
    }
    erase_line_contents(console, *length);

    *length = 0;
    line[0] = '\0';

    while (replacement[*length] != '\0' && *length < shell_line_capacity - 1U) {
        line[*length] = replacement[*length];
        write_byte_echo(console, (uint8_t)replacement[*length]);
        *length = *length + 1U;
    }

    line[*length] = '\0';
    *cursor = *length;
}

static const char *argument_after_command(const char *line) {
    size_t index = 0;

    while (line[index] != '\0' && line[index] != ' ') {
        index++;
    }

    while (line[index] == ' ') {
        index++;
    }

    return &line[index];
}

static int argument_equals(const char *argument, const char *expected) {
    size_t index = 0;

    while (argument[index] != '\0' && expected[index] != '\0' &&
        argument[index] == expected[index]) {
        index++;
    }
    if (expected[index] != '\0') {
        return 0;
    }
    while (argument[index] == ' ') {
        index++;
    }
    return argument[index] == '\0';
}

static int copy_string(char *destination, size_t capacity, const char *source) {
    size_t index = 0;

    if (capacity == 0) {
        return 0;
    }

    while (source[index] != '\0') {
        if (index >= capacity - 1U) {
            destination[0] = '\0';
            return 0;
        }

        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
    return 1;
}

static int copy_sized_string(
    char *destination,
    size_t capacity,
    const char *source,
    size_t source_length
) {
    if (capacity == 0 || source_length >= capacity) {
        return 0;
    }

    for (size_t index = 0; index < source_length; index++) {
        destination[index] = source[index];
    }

    destination[source_length] = '\0';
    return 1;
}

static int copy_first_argument(char *destination, size_t capacity, const char *argument) {
    size_t length = 0;

    if (capacity == 0) {
        return 0;
    }

    while (argument[length] != '\0' && argument[length] != ' ') {
        if (length >= capacity - 1U) {
            destination[0] = '\0';
            return 0;
        }

        destination[length] = argument[length];
        length++;
    }

    destination[length] = '\0';
    return 1;
}

static void history_add(struct shell_history *history, const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (history->count > 0U && arwill_text_equals(history->entries[history->count - 1U], line)) {
        return;
    }

    if (history->count >= shell_history_capacity) {
        for (size_t index = 1; index < shell_history_capacity; index++) {
            (void)copy_string(
                history->entries[index - 1U],
                shell_line_capacity,
                history->entries[index]
            );
        }

        history->count = shell_history_capacity - 1U;
    }

    if (copy_string(history->entries[history->count], shell_line_capacity, line)) {
        history->count++;
    }
}

static void history_previous(
    const struct arwill_console *console,
    const struct shell_history *history,
    char *line,
    size_t *length,
    size_t *cursor,
    size_t *history_position
) {
    if (history->count == 0U) {
        return;
    }

    if (*history_position > history->count) {
        *history_position = history->count;
    }

    if (*history_position == 0U) {
        return;
    }

    *history_position = *history_position - 1U;
    replace_line(
        console, line, length, cursor, history->entries[*history_position]
    );
}

static void history_next(
    const struct arwill_console *console,
    const struct shell_history *history,
    char *line,
    size_t *length,
    size_t *cursor,
    size_t *history_position
) {
    if (history->count == 0U || *history_position >= history->count) {
        return;
    }

    *history_position = *history_position + 1U;

    if (*history_position == history->count) {
        replace_line(console, line, length, cursor, "");
        return;
    }

    replace_line(
        console, line, length, cursor, history->entries[*history_position]
    );
}

static void path_set_root(char *path, size_t capacity) {
    if (capacity >= 2U) {
        path[0] = '/';
        path[1] = '\0';
    }
}

static void path_pop_segment(char *path) {
    size_t length = arwill_text_length(path);

    if (length <= 1U) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }

    while (length > 1U && path[length - 1U] == '/') {
        length--;
    }

    while (length > 1U && path[length - 1U] != '/') {
        length--;
    }

    if (length <= 1U) {
        path[1] = '\0';
    } else {
        path[length - 1U] = '\0';
    }
}

static int path_append_segment(
    char *path,
    size_t capacity,
    const char *segment,
    size_t segment_length
) {
    if (segment_length == 0U) {
        return 1;
    }

    if (segment_length == 1U && segment[0] == '.') {
        return 1;
    }

    if (segment_length == 2U && segment[0] == '.' && segment[1] == '.') {
        path_pop_segment(path);
        return 1;
    }

    size_t path_length = arwill_text_length(path);

    if (!(path_length == 1U && path[0] == '/')) {
        if (path_length >= capacity - 1U) {
            return 0;
        }

        path[path_length] = '/';
        path_length++;
        path[path_length] = '\0';
    }

    if (path_length + segment_length >= capacity) {
        return 0;
    }

    for (size_t index = 0; index < segment_length; index++) {
        path[path_length + index] = segment[index];
    }

    path[path_length + segment_length] = '\0';
    return 1;
}

static int resolve_path(
    const char *current_directory,
    const char *input_path,
    char *resolved_path,
    size_t capacity
) {
    const char *path = input_path;
    size_t index = 0;

    if (path[0] == '\0') {
        path = ".";
    }

    if (path[0] == '/') {
        path_set_root(resolved_path, capacity);
        index = 1;
    } else if (!copy_string(resolved_path, capacity, current_directory)) {
        return 0;
    }

    while (path[index] != '\0') {
        const size_t segment_start = index;

        while (path[index] != '\0' && path[index] != '/') {
            index++;
        }

        if (!path_append_segment(
                resolved_path,
                capacity,
                &path[segment_start],
                index - segment_start
            )) {
            return 0;
        }

        while (path[index] == '/') {
            index++;
        }
    }

    return 1;
}

static int string_contains(const char *value, char character) {
    for (size_t index = 0; value[index] != '\0'; index++) {
        if (value[index] == character) {
            return 1;
        }
    }
    return 0;
}

static int string_ends_with(const char *value, const char *suffix) {
    const size_t value_length = arwill_text_length(value);
    const size_t suffix_length = arwill_text_length(suffix);

    if (suffix_length > value_length) {
        return 0;
    }
    return arwill_text_equals(&value[value_length - suffix_length], suffix);
}

static int resolve_program_image_path(
    const char *current_directory,
    const char *program,
    char *resolved_path,
    size_t capacity
) {
    static const char application_directory[] = "/apps/";
    static const char program_suffix[] = ".awp";

    if (string_contains(program, '/') || string_ends_with(program, program_suffix)) {
        return resolve_path(current_directory, program, resolved_path, capacity);
    }

    const size_t directory_length = sizeof(application_directory) - 1U;
    const size_t program_length = arwill_text_length(program);
    const size_t suffix_length = sizeof(program_suffix) - 1U;

    if (directory_length + program_length + suffix_length >= capacity) {
        return 0;
    }

    size_t output = 0;
    for (size_t index = 0; index < directory_length; index++) {
        resolved_path[output++] = application_directory[index];
    }
    for (size_t index = 0; index < program_length; index++) {
        resolved_path[output++] = program[index];
    }
    for (size_t index = 0; index < suffix_length; index++) {
        resolved_path[output++] = program_suffix[index];
    }
    resolved_path[output] = '\0';
    return 1;
}

static void write_prompt(const struct arwill_console *console, const char *current_directory) {
    arwill_console_write(console, "Arwill:");
    arwill_console_write(console, current_directory);
    arwill_console_write(console, "> ");
}

static void redraw_line(
    const struct arwill_console *console,
    const char *current_directory,
    const char *line
) {
    write_prompt(console, current_directory);
    arwill_console_write(console, line);
}

static void print_help(const struct arwill_console *console, int remote_session) {
    arwill_console_write_line(console, "commands:");
    arwill_console_write_line(console, "  help       show commands");
    arwill_console_write_line(console, "  version    show kernel version");
    arwill_console_write_line(console, "  system     show system state and subsystem details");
    arwill_console_write_line(console, "  devices    list devices or inspect pci/disk0/net0");
    arwill_console_write_line(console, "  network    show network state, ping, or TCP details");
    arwill_console_write_line(console, "  top        show a live system and process dashboard");
    arwill_console_write_line(console, "  pwd        show current directory");
    arwill_console_write_line(console, "  cd [path]  change current directory");
    arwill_console_write_line(console, "  clear      clear the terminal screen");
    arwill_console_write_line(console, "  ls [path]  list the current filesystem");
    arwill_console_write_line(console, "  cat [path] show text file contents");
    arwill_console_write_line(console, "  mkdir [path] create a directory");
    arwill_console_write_line(console, "  rm [path] remove a file or empty directory");
    arwill_console_write_line(console, "  stat [path] show file or directory metadata");
    arwill_console_write_line(console, "  config     show or change system configuration");
    arwill_console_write_line(console, "  logs       show the complete event log");
    arwill_console_write_line(console, "  service    inspect or control built-in services");
    arwill_console_write_line(console, "  ps         show system, kernel, and AWP tasks");
    arwill_console_write_line(console, "  run [name] launch a built-in kernel process");
    arwill_console_write_line(console, "  exec [program] [file] run a stored program image");
    arwill_console_write_line(console, remote_session ?
        "  exit       close the remote session" :
        "  exit       power off the machine");
    arwill_console_write_line(
        console, "  Tab        complete commands, arguments, paths, and processes"
    );
    arwill_console_write_line(console, "  Up/Down    browse command history");
    arwill_console_write_line(console, "  Left/Right edit the current command line");
    arwill_console_write_line(console, "  halt       enter the CPU idle loop");
}

static void print_version(const struct arwill_console *console) {
    arwill_console_write(console, ARWILL_PROJECT_NAME);
    arwill_console_write(console, " ");
    arwill_console_write_line(console, ARWILL_PROJECT_VERSION);
}

static void print_uptime(
    const struct arwill_console *console,
    const struct arwill_clock *clock
) {
    arwill_console_write(console, "uptime: ");
    write_uint64_decimal(console, arwill_clock_monotonic_milliseconds(clock));
    arwill_console_write_line(console, " ms");
}

static void print_pci_info(
    const struct arwill_console *console,
    const struct arwill_pci_bus *pci
) {
    arwill_console_write_line(console, "pci: x86_64 configuration mechanism 1");
    arwill_console_write(console, "devices: ");
    write_size_decimal(console, pci == 0 ? 0U : pci->count);
    arwill_console_write_line(console, "");
    if (pci == 0) {
        return;
    }
    for (size_t index = 0; index < pci->count; index++) {
        const struct arwill_pci_device *device = &pci->devices[index];
        arwill_console_write(console, "  vendor ");
        write_uint64_hex(console, device->vendor_id);
        arwill_console_write(console, " device ");
        write_uint64_hex(console, device->device_id);
        arwill_console_write(console, " bar0 ");
        write_uint64_hex(console, device->bars[0]);
        arwill_console_write(console, " bar1 ");
        write_uint64_hex(console, device->bars[1]);
        arwill_console_write(console, " class ");
        write_uint64_hex(console, device->class_code);
        arwill_console_write(console, "/");
        write_uint64_hex(console, device->subclass);
        arwill_console_write_line(console, "");
    }
}

static void print_network_device_info(
    const struct arwill_console *console,
    const struct arwill_network_device *network
) {
    uint8_t mac[arwill_network_mac_length];

    arwill_console_write(console, "network: ");
    if (network == 0 || network->name == 0) {
        arwill_console_write_line(console, "unavailable");
        return;
    }
    arwill_console_write_line(console, network->name);
    arwill_console_write(console, "mac: ");
    if (!arwill_network_read_mac(network, mac)) {
        arwill_console_write_line(console, "unavailable");
        return;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        if (index != 0U) {
            arwill_console_write(console, ":");
        }
        write_uint8_hex(console, mac[index]);
    }
    arwill_console_write_line(console, "");
    arwill_console_write_line(console, "frame path: tx/rx bounded polling ready");
}

static void print_tcp_info(
    const struct arwill_console *console,
    const struct arwill_ipv4_stack *ipv4
) {
    if (ipv4 == 0) {
        arwill_console_write_line(console, "tcp: unavailable");
        return;
    }
    struct arwill_tcp_endpoint_snapshot remote;
    if (!arwill_ipv4_tcp_endpoint_snapshot(ipv4, 0U, &remote)) {
        arwill_console_write_line(console, "tcp: unavailable");
        return;
    }
    arwill_console_write(console, "tcp remote-console: port ");
    write_uint64_decimal(console, remote.local_port);
    arwill_console_write(console, ", state ");
    arwill_console_write_line(console, remote.state);
    size_t allocated = 0;
    size_t listening = 0;
    size_t connected = 0;
    for (size_t index = 0; index < arwill_tcp_endpoint_capacity; index++) {
        struct arwill_tcp_endpoint_snapshot endpoint;
        if (!arwill_ipv4_tcp_endpoint_snapshot(ipv4, index, &endpoint) ||
            !endpoint.allocated) {
            continue;
        }
        allocated++;
        if (endpoint.listening) {
            listening++;
        }
        if (endpoint.connected) {
            connected++;
        }
        arwill_console_write(console, "tcp endpoint ");
        write_size_decimal(console, index);
        arwill_console_write(console, ": owner ");
        arwill_console_write(console, endpoint.owner);
        arwill_console_write(console, ", local :");
        write_uint64_decimal(console, endpoint.local_port);
        arwill_console_write(console, ", peer ");
        if (endpoint.peer_port == 0U) {
            arwill_console_write(console, "-");
        } else {
            write_ipv4_address(console, endpoint.peer_address);
            arwill_console_write(console, ":");
            write_uint64_decimal(console, endpoint.peer_port);
        }
        arwill_console_write(console, ", state ");
        arwill_console_write_line(console, endpoint.state);
    }
    arwill_console_write(console, "tcp endpoints: allocated ");
    write_uint64_decimal(console, allocated);
    arwill_console_write(console, "/");
    write_uint64_decimal(console, arwill_tcp_endpoint_capacity);
    arwill_console_write(console, ", listening ");
    write_uint64_decimal(console, listening);
    arwill_console_write(console, ", connected ");
    write_uint64_decimal(console, connected);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp frames: received ");
    write_uint64_decimal(console, ipv4->tcp_frames_received);
    arwill_console_write(console, ", sent ");
    write_uint64_decimal(console, ipv4->tcp_frames_sent);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp bytes: received ");
    write_uint64_decimal(console, ipv4->tcp_bytes_received);
    arwill_console_write(console, ", sent ");
    write_uint64_decimal(console, ipv4->tcp_bytes_sent);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp control: syn ");
    write_uint64_decimal(console, ipv4->tcp_syn_received);
    arwill_console_write(console, ", syn-ack ");
    write_uint64_decimal(console, ipv4->tcp_syn_ack_sent);
    arwill_console_write(console, ", fin ");
    write_uint64_decimal(console, ipv4->tcp_fin_received);
    arwill_console_write(console, ", rst received ");
    write_uint64_decimal(console, ipv4->tcp_rst_received);
    arwill_console_write(console, ", sent ");
    write_uint64_decimal(console, ipv4->tcp_rst_sent);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp dispatch: unknown port ");
    write_uint64_decimal(console, ipv4->tcp_unknown_port_frames);
    arwill_console_write(console, ", tuple mismatch ");
    write_uint64_decimal(console, ipv4->tcp_tuple_mismatches);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "tcp flow: receive window ");
    write_uint64_decimal(console, remote.receive_available);
    arwill_console_write(console, "/");
    write_uint64_decimal(console, arwill_tcp_stream_receive_capacity);
    arwill_console_write(console, ", peer window ");
    write_uint64_decimal(console, remote.peer_window);
    arwill_console_write(console, ", peer mss ");
    write_uint64_decimal(console, remote.peer_maximum_segment_size);
    arwill_console_write(console, ", drops ");
    write_uint64_decimal(console, ipv4->tcp_receive_window_drops);
    arwill_console_write(console, ", updates ");
    write_uint64_decimal(console, ipv4->tcp_window_updates);
    arwill_console_write_line(console, "");
    print_remote_console_info(console, ipv4);
}

static void print_icmp_info(
    const struct arwill_console *console,
    const struct arwill_ipv4_stack *ipv4
) {
    if (ipv4 == 0) {
        arwill_console_write_line(console, "icmp: unavailable");
        return;
    }
    arwill_console_write(console, "icmp echo: requests ");
    write_uint64_decimal(console, ipv4->icmp_echo_requests_received);
    arwill_console_write(console, ", replies ");
    write_uint64_decimal(console, ipv4->icmp_echo_replies_sent);
    arwill_console_write(console, ", checksum drops ");
    write_uint64_decimal(console, ipv4->icmp_checksum_drops);
    arwill_console_write_line(console, "");
}

static const char *remote_tcp_state_name(const struct arwill_ipv4_stack *ipv4) {
    struct arwill_tcp_endpoint_snapshot remote;
    return arwill_ipv4_tcp_endpoint_snapshot(ipv4, 0U, &remote)
        ? remote.state : "unavailable";
}

static void ping_network(
    const struct arwill_console *console,
    struct arwill_ipv4_stack *ipv4
) {
    arwill_console_write_line(console, "ping 10.0.2.2");
    if (ipv4 == 0 || !arwill_ipv4_ping_gateway(ipv4)) {
        if (ipv4 != 0 && ipv4->gateway_resolved) {
            arwill_console_write_line(console, "ping: ICMP no reply");
        } else {
            arwill_console_write_line(console, "ping: ARP no reply");
        }
        return;
    }
    arwill_console_write_line(console, "ping: reply received");
}

static void print_listing(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    const char *path
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), path)) {
        arwill_console_write_line(console, "ls: path too long");
        return;
    }

    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write_line(console, "ls: path too long");
        return;
    }

    struct arwill_fs_listing listing;

    if (!arwill_filesystem_list(filesystem, resolved_path, &listing)) {
        arwill_console_write(console, "ls: no such directory: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    for (size_t index = 0; index < listing.count; index++) {
        const struct arwill_fs_entry *entry = &listing.entries[index];

        arwill_console_write(console, entry->name);
        if (entry->type == arwill_fs_entry_directory) {
            arwill_console_write(console, "/");
        }
        arwill_console_write_line(console, "");
    }
}

static void change_directory(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    char *current_directory,
    const char *path
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), path)) {
        arwill_console_write_line(console, "cd: path too long");
        return;
    }

    if (path_argument[0] == '\0') {
        (void)copy_string(path_argument, sizeof(path_argument), "/");
    }

    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write_line(console, "cd: path too long");
        return;
    }

    struct arwill_fs_listing listing;

    if (!arwill_filesystem_list(filesystem, resolved_path, &listing)) {
        arwill_console_write(console, "cd: no such directory: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    if (!copy_string(current_directory, shell_path_capacity, resolved_path)) {
        arwill_console_write_line(console, "cd: path too long");
    }
}

static void print_file(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    const char *path
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), path)) {
        arwill_console_write_line(console, "cat: path too long");
        return;
    }

    if (path_argument[0] == '\0') {
        arwill_console_write_line(console, "cat: missing path");
        return;
    }

    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write_line(console, "cat: path too long");
        return;
    }

    struct arwill_fs_file file;

    if (!arwill_filesystem_read_file(filesystem, resolved_path, &file)) {
        struct arwill_fs_listing listing;

        if (arwill_filesystem_list(filesystem, resolved_path, &listing)) {
            arwill_console_write(console, "cat: is a directory: ");
            arwill_console_write_line(console, resolved_path);
            return;
        }

        arwill_console_write(console, "cat: no such file: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    if (file.type != arwill_fs_file_text || file.contents == 0) {
        arwill_console_write(console, "cat: cannot display binary file: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    arwill_console_write(console, file.contents);

    const size_t contents_length = arwill_text_length(file.contents);
    if (contents_length == 0U || file.contents[contents_length - 1U] != '\n') {
        arwill_console_write_line(console, "");
    }
}

static uint32_t exec_program_image(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const struct arwill_user_runtime *user_runtime,
    const char *current_directory,
    const char *path
) {
    char path_argument[shell_path_capacity];
    char launch_argument[arwill_user_argument_capacity];
    char resolved_path[shell_path_capacity];
    char resolved_launch_argument[arwill_user_argument_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), path)) {
        arwill_console_write_line(console, "exec: path too long");
        return 0;
    }

    if (path_argument[0] == '\0') {
        arwill_console_write_line(console, "exec: missing path");
        return 0;
    }

    const char *argument = argument_after_command(path);
    if (!copy_first_argument(
            launch_argument, sizeof(launch_argument), argument
        )) {
        arwill_console_write_line(console, "exec: argument too long");
        return 0;
    }
    if (argument_after_command(argument)[0] != '\0') {
        arwill_console_write_line(console, "exec: too many arguments");
        return 0;
    }

    resolved_launch_argument[0] = '\0';
    if (launch_argument[0] != '\0' &&
        !resolve_path(
            current_directory,
            launch_argument,
            resolved_launch_argument,
            sizeof(resolved_launch_argument)
        )) {
        arwill_console_write_line(console, "exec: file path too long");
        return 0;
    }

    if (!resolve_program_image_path(
            current_directory,
            path_argument,
            resolved_path,
            sizeof(resolved_path)
        )) {
        arwill_console_write_line(console, "exec: path too long");
        return 0;
    }

    struct arwill_fs_file file;

    if (!arwill_filesystem_read_file(filesystem, resolved_path, &file)) {
        arwill_console_write(console, "exec: no such file: ");
        arwill_console_write_line(console, resolved_path);
        return 0;
    }

    if (file.type != arwill_fs_file_binary || file.contents == 0) {
        arwill_console_write(console, "exec: not a program image: ");
        arwill_console_write_line(console, resolved_path);
        return 0;
    }

    uint32_t pid = 0;
    if (!arwill_user_spawn_image(
            user_runtime,
            (const uint8_t *)file.contents,
            file.size_bytes,
            resolved_path,
            resolved_launch_argument,
            console,
            &pid
        )) {
        arwill_console_write(console, "exec: launch failed: ");
        arwill_console_write_line(console, resolved_path);
        return 0;
    }
    arwill_console_write(console, "exec: spawned pid ");
    write_uint64_decimal(console, pid);
    arwill_console_write_line(console, "");
    return pid;
}

static const char *second_argument_after_first(const char *argument) {
    size_t index = 0;

    while (argument[index] != '\0' && argument[index] != ' ') {
        index++;
    }

    while (argument[index] == ' ') {
        index++;
    }

    return &argument[index];
}

static void write_file(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    const char *argument
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];
    const char *contents = second_argument_after_first(argument);

    if (!copy_first_argument(path_argument, sizeof(path_argument), argument)) {
        arwill_console_write_line(console, "write: path too long");
        return;
    }

    if (path_argument[0] == '\0') {
        arwill_console_write_line(console, "write: missing path");
        return;
    }

    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write_line(console, "write: path too long");
        return;
    }

    if (!arwill_filesystem_write_bytes(
            filesystem,
            resolved_path,
            arwill_fs_file_text,
            (const uint8_t *)contents,
            arwill_text_length(contents))) {
        arwill_console_write(console, "write: cannot write: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    arwill_console_write(console, "write: wrote ");
    write_uint64_decimal(console, (uint64_t)arwill_text_length(contents));
    arwill_console_write(console, " bytes to ");
    arwill_console_write_line(console, resolved_path);
}

static int hex_nibble(char value, uint8_t *nibble) {
    if (value >= '0' && value <= '9') {
        *nibble = (uint8_t)(value - '0');
        return 1;
    }
    if (value >= 'a' && value <= 'f') {
        *nibble = (uint8_t)(value - 'a' + 10);
        return 1;
    }
    if (value >= 'A' && value <= 'F') {
        *nibble = (uint8_t)(value - 'A' + 10);
        return 1;
    }
    return 0;
}

static void write_hex_file(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    const char *argument
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];
    uint8_t bytes[shell_line_capacity / 2U];
    const char *hex = second_argument_after_first(argument);
    const size_t hex_length = arwill_text_length(hex);

    if (!copy_first_argument(path_argument, sizeof(path_argument), argument)) {
        arwill_console_write_line(console, "writehex: path too long");
        return;
    }
    if (path_argument[0] == '\0') {
        arwill_console_write_line(console, "writehex: missing path");
        return;
    }
    if (hex_length == 0U || (hex_length % 2U) != 0U) {
        arwill_console_write_line(console, "writehex: hex must contain complete bytes");
        return;
    }
    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write_line(console, "writehex: path too long");
        return;
    }

    const size_t byte_count = hex_length / 2U;
    for (size_t index = 0; index < byte_count; index++) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!hex_nibble(hex[index * 2U], &high) ||
            !hex_nibble(hex[index * 2U + 1U], &low)) {
            arwill_console_write_line(console, "writehex: invalid hex digit");
            return;
        }
        bytes[index] = (uint8_t)((uint8_t)(high << 4U) | low);
    }

    if (!arwill_filesystem_write_bytes(
            filesystem, resolved_path, arwill_fs_file_binary, bytes, byte_count)) {
        arwill_console_write(console, "writehex: cannot write: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    arwill_console_write(console, "writehex: wrote ");
    write_uint64_decimal(console, (uint64_t)byte_count);
    arwill_console_write(console, " bytes to ");
    arwill_console_write_line(console, resolved_path);
}

static void mutate_path(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    const char *argument,
    const char *command,
    int create
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), argument)) {
        arwill_console_write(console, command);
        arwill_console_write_line(console, ": path too long");
        return;
    }
    if (path_argument[0] == '\0') {
        arwill_console_write(console, command);
        arwill_console_write_line(console, ": missing path");
        return;
    }
    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write(console, command);
        arwill_console_write_line(console, ": path too long");
        return;
    }

    const int changed = create
        ? arwill_filesystem_create_directory(filesystem, resolved_path)
        : arwill_filesystem_remove(filesystem, resolved_path);
    if (!changed) {
        arwill_console_write(console, command);
        arwill_console_write(console, create ? ": cannot create: " : ": cannot remove: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    arwill_console_write(console, command);
    arwill_console_write(console, create ? ": created " : ": removed ");
    arwill_console_write_line(console, resolved_path);
}

static void clear_screen(const struct arwill_console *console) {
    arwill_console_write(console, "\x1b[2J\x1b[H");
}

static void print_stat(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    const char *path,
    const char *command_name
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), path)) {
        arwill_console_write(console, command_name);
        arwill_console_write_line(console, ": path too long");
        return;
    }

    if (path_argument[0] == '\0') {
        arwill_console_write(console, command_name);
        arwill_console_write_line(console, ": missing path");
        return;
    }

    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write(console, command_name);
        arwill_console_write_line(console, ": path too long");
        return;
    }

    struct arwill_fs_listing listing;

    if (arwill_filesystem_list(filesystem, resolved_path, &listing)) {
        arwill_console_write(console, "path: ");
        arwill_console_write_line(console, resolved_path);
        arwill_console_write_line(console, "type: directory");
        arwill_console_write(console, "entries: ");
        write_size_decimal(console, listing.count);
        arwill_console_write_line(console, "");
        return;
    }

    struct arwill_fs_file file;

    if (!arwill_filesystem_read_file(filesystem, resolved_path, &file)) {
        arwill_console_write(console, command_name);
        arwill_console_write(console, ": no such file: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    arwill_console_write(console, "path: ");
    arwill_console_write_line(console, resolved_path);
    arwill_console_write(console, "type: ");

    if (file.type == arwill_fs_file_text) {
        arwill_console_write_line(console, "text file");
        arwill_console_write(console, "size: ");
        write_uint64_decimal(console, file.size_bytes);
        arwill_console_write_line(console, " bytes");
        return;
    }

    arwill_console_write_line(console, "binary file");
    arwill_console_write(console, "size: ");
    write_uint64_decimal(console, file.size_bytes);
    arwill_console_write_line(console, " bytes");
}

static void print_memory_region(
    const struct arwill_console *console,
    const struct arwill_memory_region *region
) {
    arwill_console_write(console, "  ");
    arwill_console_write(console, arwill_memory_region_type_name(region->type));
    arwill_console_write(console, " ");
    write_uint64_hex(console, region->base);
    arwill_console_write(console, "-");
    write_uint64_hex(console, saturating_add_uint64(region->base, region->length));
    arwill_console_write(console, " ");
    write_uint64_decimal(console, region->length);
    arwill_console_write_line(console, " bytes");
}

static void print_yes_no(const struct arwill_console *console, int value) {
    if (value) {
        arwill_console_write_line(console, "yes");
    } else {
        arwill_console_write_line(console, "no");
    }
}

static void print_meminfo(
    const struct arwill_console *console,
    const struct arwill_memory *memory
) {
    const struct arwill_memory_map *map = arwill_memory_map(memory);

    if (map == 0 || map->regions == 0 || map->count == 0U) {
        arwill_console_write_line(console, "memory map: unavailable");
    } else {
        arwill_console_write_line(console, "memory map:");

        for (size_t index = 0; index < map->count; index++) {
            print_memory_region(console, &map->regions[index]);
        }

        if (map->truncated) {
            arwill_console_write_line(console, "  warning: memory map truncated");
        }
    }

    struct arwill_physical_allocator_stats stats;
    arwill_physical_allocator_stats(memory, &stats);

    arwill_console_write_line(console, "physical allocator:");
    arwill_console_write(console, "  page size: ");
    write_uint64_decimal(console, stats.page_size);
    arwill_console_write_line(console, " bytes");
    arwill_console_write(console, "  ranges: ");
    write_size_decimal(console, stats.range_count);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "  total pages: ");
    write_uint64_decimal(console, stats.total_pages);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "  free pages: ");
    write_uint64_decimal(console, stats.free_pages);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "  allocated pages: ");
    write_uint64_decimal(console, stats.allocated_pages);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "  allocations: ");
    write_uint64_decimal(console, stats.allocation_count);
    arwill_console_write_line(console, "");

    struct arwill_kernel_heap_stats heap_stats;
    arwill_kernel_heap_stats(memory, &heap_stats);

    arwill_console_write_line(console, "kernel heap:");
    arwill_console_write(console, "  initialized: ");
    print_yes_no(console, heap_stats.initialized);
    arwill_console_write(console, "  size: ");
    write_size_decimal(console, heap_stats.size_bytes);
    arwill_console_write_line(console, " bytes");
    arwill_console_write(console, "  used: ");
    write_size_decimal(console, heap_stats.used_bytes);
    arwill_console_write_line(console, " bytes");
    arwill_console_write(console, "  free: ");
    write_size_decimal(console, heap_stats.free_bytes);
    arwill_console_write_line(console, " bytes");
    arwill_console_write(console, "  allocations: ");
    write_size_decimal(console, heap_stats.allocation_count);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "  frees: ");
    write_size_decimal(console, heap_stats.free_count);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "  failed allocations: ");
    write_size_decimal(console, heap_stats.failed_allocation_count);
    arwill_console_write_line(console, "");
}

static void run_heap_test(
    const struct arwill_console *console,
    struct arwill_memory *memory
) {
    void *small = arwill_kmalloc(memory, 24);
    void *large = arwill_kmalloc(memory, 128);

    if (small == 0 || large == 0) {
        if (small != 0) {
            arwill_kfree(memory, small);
        }

        if (large != 0) {
            arwill_kfree(memory, large);
        }

        arwill_console_write_line(console, "heaptest: allocation failed");
        return;
    }

    arwill_kfree(memory, large);
    arwill_kfree(memory, small);
    arwill_console_write_line(console, "heaptest: allocated and freed 2 blocks");

    struct arwill_kernel_heap_stats stats;
    arwill_kernel_heap_stats(memory, &stats);

    arwill_console_write(console, "heaptest: allocations ");
    write_size_decimal(console, stats.allocation_count);
    arwill_console_write(console, ", frees ");
    write_size_decimal(console, stats.free_count);
    arwill_console_write_line(console, "");
}

static void print_devices(
    const struct arwill_console *console,
    const struct arwill_device_registry *devices
) {
    size_t count = 0;
    const struct arwill_device_entry *entries = arwill_device_entries(devices, &count);

    if (entries == 0) {
        arwill_console_write_line(console, "devices: unavailable");
        return;
    }

    arwill_console_write_line(console, "name kind driver status");

    for (size_t index = 0; index < count; index++) {
        const struct arwill_device_entry *entry = &entries[index];

        arwill_console_write(console, entry->name);
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_device_kind_name(entry->kind));
        arwill_console_write(console, " ");
        arwill_console_write(console, entry->driver);
        arwill_console_write(console, " ");
        arwill_console_write_line(console, entry->status);
    }

    if (count == 0U) {
        arwill_console_write_line(console, "no devices");
    }
}

static int is_sample_byte(uint8_t byte) {
    return (byte >= 0x20U && byte <= 0x7eU) || byte == '\n';
}

static void print_block_sample(const struct arwill_console *console, const uint8_t *sector) {
    enum {
        sample_limit = 64
    };

    arwill_console_write(console, "sample: ");

    for (size_t index = 0; index < sample_limit; index++) {
        const uint8_t byte = sector[index];

        if (byte == '\0' || byte == '\n') {
            break;
        }

        if (is_sample_byte(byte)) {
            write_byte_echo(console, byte);
        } else {
            arwill_console_write(console, ".");
        }
    }

    arwill_console_write_line(console, "");
}

static void print_block_info(
    const struct arwill_console *console,
    const struct arwill_block_device *block_device
) {
    uint8_t sector[512];

    if (block_device == 0) {
        arwill_console_write_line(console, "block device: unavailable");
        return;
    }

    arwill_console_write(console, "block device: ");
    arwill_console_write_line(console, block_device->name);
    arwill_console_write(console, "sector size: ");
    write_uint64_decimal(console, (uint64_t)block_device->sector_size);
    arwill_console_write_line(console, " bytes");
    arwill_console_write(console, "sectors: ");
    write_uint64_decimal(console, block_device->sector_count);
    arwill_console_write_line(console, "");

    if (block_device->sector_size != sizeof(sector)) {
        arwill_console_write_line(console, "sample: unsupported sector size");
        return;
    }

    if (block_device->sector_count <= 1U) {
        arwill_console_write_line(console, "sample: disk too small");
        return;
    }

    if (!arwill_block_read(block_device, 1, 1, sector, sizeof(sector))) {
        arwill_console_write_line(console, "sample: read failed");
        return;
    }

    arwill_console_write_line(console, "sample lba: 1");
    print_block_sample(console, sector);
}

static void print_loaded_missing(const struct arwill_console *console, int value) {
    if (value) {
        arwill_console_write_line(console, "loaded");
    } else {
        arwill_console_write_line(console, "missing");
    }
}

static void print_configured_missing(const struct arwill_console *console, int value) {
    if (value) {
        arwill_console_write_line(console, "configured");
    } else {
        arwill_console_write_line(console, "missing");
    }
}

static void print_irqinfo(
    const struct arwill_console *console,
    const struct arwill_interrupts *interrupts
) {
    struct arwill_interrupt_stats stats;
    const int timer_observed = arwill_interrupts_wait_for_timer_tick(interrupts);

    arwill_interrupts_stats(interrupts, &stats);

    arwill_console_write(console, "interrupts: ");
    if (interrupts == 0 || interrupts->name == 0) {
        arwill_console_write_line(console, "unavailable");
    } else {
        arwill_console_write_line(console, interrupts->name);
    }

    arwill_console_write(console, "idt: ");
    print_loaded_missing(console, stats.idt_loaded);
    arwill_console_write(console, "pic: ");
    if (stats.pic_remapped) {
        arwill_console_write_line(console, "remapped");
    } else {
        arwill_console_write_line(console, "missing");
    }
    arwill_console_write(console, "timer: ");
    print_configured_missing(console, stats.timer_configured);
    arwill_console_write(console, "enabled: ");
    print_yes_no(console, stats.enabled);
    arwill_console_write(console, "timer observed: ");
    print_yes_no(console, timer_observed);
    arwill_console_write(console, "timer ticks: ");
    write_uint64_decimal(console, stats.timer_ticks);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "exceptions: ");
    write_uint64_decimal(console, stats.exception_count);
    arwill_console_write_line(console, "");

    if (stats.exception_count > 0U) {
        arwill_console_write(console, "last exception: ");
        write_uint64_decimal(console, (uint64_t)stats.last_exception_vector);
        arwill_console_write_line(console, "");
    }
}

static void probe_interrupts(
    const struct arwill_console *console,
    const struct arwill_interrupts *interrupts
) {
    struct arwill_interrupt_stats before;
    struct arwill_interrupt_stats after;

    arwill_interrupts_stats(interrupts, &before);
    arwill_interrupts_trigger_breakpoint(interrupts);
    arwill_interrupts_stats(interrupts, &after);

    if (
        after.exception_count > before.exception_count &&
        after.last_exception_vector == 3U
    ) {
        arwill_console_write_line(console, "exception probe: handled vector 3");
        return;
    }

    arwill_console_write_line(console, "exception probe: failed");
}

static void print_scheduler_info(
    const struct arwill_console *console,
    const struct arwill_interrupts *interrupts
) {
    struct arwill_scheduler_stats stats;

    (void)arwill_interrupts_wait_for_timer_tick(interrupts);
    arwill_scheduler_stats(&stats);

    arwill_console_write(console, "scheduler: ");
    arwill_console_write_line(console, stats.name);
    arwill_console_write(console, "scheduler ticks: ");
    write_uint64_decimal(console, stats.ticks);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "scheduler slots: ");
    write_size_decimal(console, stats.slot_count);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "current slot: ");
    write_size_decimal(console, stats.current_slot);
    arwill_console_write_line(console, "");

    for (
        size_t index = 0;
        index < stats.slot_count && index < arwill_scheduler_slot_capacity;
        index++
    ) {
        arwill_console_write(console, "slot ");
        arwill_console_write(console, stats.slots[index].name);
        arwill_console_write(console, " ticks: ");
        write_uint64_decimal(console, stats.slots[index].ticks);
        arwill_console_write_line(console, "");
    }
}

static void print_user_info(
    const struct arwill_console *console,
    const struct arwill_user_runtime *user_runtime
) {
    struct arwill_user_stats stats;

    arwill_user_runtime_stats(user_runtime, &stats);

    arwill_console_write(console, "user: ");
    if (user_runtime == 0 || user_runtime->name == 0) {
        arwill_console_write_line(console, "unavailable");
    } else {
        arwill_console_write_line(console, user_runtime->name);
    }

    arwill_console_write(console, "available: ");
    print_yes_no(console, stats.available);
    arwill_console_write(console, "hhdm: ");
    print_yes_no(console, stats.hhdm_available);
    arwill_console_write(console, "gdt: ");
    print_loaded_missing(console, stats.gdt_loaded);
    arwill_console_write(console, "tss: ");
    print_loaded_missing(console, stats.tss_loaded);
    arwill_console_write(console, "syscall gate: ");
    print_loaded_missing(console, stats.syscall_gate_loaded);
    arwill_console_write(console, "runs: ");
    write_uint64_decimal(console, stats.runs);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "syscalls: ");
    write_uint64_decimal(console, stats.syscall_count);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "bytes written: ");
    write_uint64_decimal(console, stats.bytes_written);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "bad syscalls: ");
    write_uint64_decimal(console, stats.bad_syscalls);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "user preemptions: ");
    write_uint64_decimal(console, stats.preemptions);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "user faults: ");
    write_uint64_decimal(console, stats.faults);
    arwill_console_write_line(console, "");
}

static void print_owner_info(const struct arwill_console *console) {
    arwill_console_write_line(console, "owner model: " ARWILL_OWNER_MODEL);
    arwill_console_write_line(console, "accounts: none");
    arwill_console_write_line(console, "owner access: full system control");
    arwill_console_write_line(console, "kernel boundary: ring 3 programs use syscalls");
    arwill_console_write_line(console, "privileged code: explicit kernel or driver work");
}

static size_t process_count_kind(
    const struct arwill_process_manager *processes,
    enum arwill_process_kind kind
) {
    const struct arwill_process *table = arwill_process_table(processes);
    size_t count = 0;

    if (table == 0) {
        return 0;
    }
    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        if (
            table[index].state != arwill_process_state_empty &&
            table[index].kind == kind
        ) {
            count++;
        }
    }
    return count;
}

static void print_system_summary(
    const struct arwill_console *console,
    const struct arwill_clock *clock,
    const struct arwill_memory *memory,
    const struct arwill_process_manager *processes,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_service_manager *services
) {
    struct arwill_physical_allocator_stats physical;
    struct arwill_kernel_heap_stats heap;
    struct arwill_scheduler_stats scheduler;
    struct arwill_user_task_info tasks[arwill_user_task_capacity];
    const size_t task_count = arwill_user_tasks(
        user_runtime, tasks, arwill_user_task_capacity
    );

    arwill_physical_allocator_stats(memory, &physical);
    arwill_kernel_heap_stats(memory, &heap);
    arwill_scheduler_stats(&scheduler);

    arwill_console_write_line(
        console, "system: " ARWILL_PROJECT_NAME " " ARWILL_PROJECT_VERSION
    );
    print_uptime(console, clock);
    arwill_console_write_line(console, "owner model: " ARWILL_OWNER_MODEL);
    arwill_console_write(console, "memory: pages ");
    write_uint64_decimal(console, physical.free_pages);
    arwill_console_write(console, "/");
    write_uint64_decimal(console, physical.total_pages);
    arwill_console_write(console, " free, heap ");
    write_size_decimal(console, heap.used_bytes);
    arwill_console_write(console, "/");
    write_size_decimal(console, heap.size_bytes);
    arwill_console_write_line(console, " bytes used");
    arwill_console_write(console, "scheduler: ticks ");
    write_uint64_decimal(console, scheduler.ticks);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "processes: system ");
    write_size_decimal(
        console, process_count_kind(processes, arwill_process_kind_system)
    );
    arwill_console_write(console, ", kernel ");
    write_size_decimal(
        console, process_count_kind(processes, arwill_process_kind_kernel)
    );
    arwill_console_write(console, ", awp ");
    write_size_decimal(console, task_count);
    arwill_console_write(console, "/");
    write_size_decimal(console, arwill_user_task_capacity);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "remote-console: ");
    arwill_console_write_line(
        console,
        services == 0 ? "unavailable" :
            arwill_service_state_name(services->remote_console_state)
    );
}

static void print_storage_info(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem
) {
    struct arwill_fs_storage_stats stats;

    arwill_console_write(console, "storage: ");
    if (!arwill_filesystem_storage_stats(filesystem, &stats)) {
        arwill_console_write_line(console, "unavailable");
        return;
    }
    arwill_console_write_line(
        console,
        filesystem == 0 || filesystem->name == 0 ? "unknown" : filesystem->name
    );
    arwill_console_write(console, "entries: ");
    write_size_decimal(console, stats.entries_used);
    arwill_console_write(console, "/");
    write_size_decimal(console, stats.entries_capacity);
    arwill_console_write_line(console, " used");
    arwill_console_write(console, "data sectors: ");
    write_uint64_decimal(console, stats.used_data_sectors);
    arwill_console_write(console, " used, ");
    write_uint64_decimal(console, stats.free_data_sectors);
    arwill_console_write(console, " free, ");
    write_uint64_decimal(console, stats.data_sectors);
    arwill_console_write_line(console, " total");
    arwill_console_write(console, "largest free run: ");
    write_uint64_decimal(console, stats.largest_free_run_sectors);
    arwill_console_write_line(console, " sectors");
    arwill_console_write(console, "manifest: ");
    write_uint64_decimal(console, stats.manifest_sectors);
    arwill_console_write_line(console, " sectors");
    arwill_console_write(console, "limits: path ");
    write_size_decimal(console, stats.max_path_bytes);
    arwill_console_write(console, " bytes, file ");
    write_size_decimal(console, stats.max_file_bytes);
    arwill_console_write_line(console, " bytes");
}

static void run_system_command(
    const struct arwill_console *console,
    const char *argument,
    const struct arwill_clock *clock,
    const struct arwill_filesystem *filesystem,
    const struct arwill_memory *memory,
    const struct arwill_interrupts *interrupts,
    const struct arwill_process_manager *processes,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_service_manager *services
) {
    if (argument[0] == '\0') {
        print_system_summary(
            console, clock, memory, processes, user_runtime, services
        );
        return;
    }
    if (argument_equals(argument, "memory")) {
        print_meminfo(console, memory);
        return;
    }
    if (argument_equals(argument, "storage")) {
        print_storage_info(console, filesystem);
        return;
    }
    if (argument_equals(argument, "interrupts")) {
        print_irqinfo(console, interrupts);
        return;
    }
    if (argument_equals(argument, "scheduler")) {
        print_scheduler_info(console, interrupts);
        return;
    }
    if (argument_equals(argument, "runtime")) {
        print_user_info(console, user_runtime);
        return;
    }
    if (argument_equals(argument, "owner")) {
        print_owner_info(console);
        return;
    }
    arwill_console_write_line(
        console,
        "system: expected memory, storage, interrupts, scheduler, runtime, or owner"
    );
}

static void run_devices_command(
    const struct arwill_console *console,
    const char *argument,
    const struct arwill_device_registry *devices,
    const struct arwill_pci_bus *pci,
    const struct arwill_block_device *block_device,
    const struct arwill_network_device *network
) {
    if (argument[0] == '\0') {
        print_devices(console, devices);
        return;
    }
    if (argument_equals(argument, "pci")) {
        print_pci_info(console, pci);
        return;
    }
    if (argument_equals(argument, "disk0")) {
        print_block_info(console, block_device);
        return;
    }
    if (argument_equals(argument, "net0")) {
        print_network_device_info(console, network);
        return;
    }
    arwill_console_write_line(console, "devices: expected pci, disk0, or net0");
}

static void run_network_command(
    const struct arwill_console *console,
    const char *argument,
    const struct arwill_network_device *network,
    struct arwill_ipv4_stack *ipv4
) {
    if (argument[0] == '\0') {
        print_network_device_info(console, network);
        arwill_ipv4_print_config(ipv4, console);
        print_icmp_info(console, ipv4);
        print_tcp_info(console, ipv4);
        return;
    }
    if (argument_equals(argument, "ping")) {
        ping_network(console, ipv4);
        return;
    }
    if (argument_equals(argument, "tcp")) {
        print_tcp_info(console, ipv4);
        return;
    }
    arwill_console_write_line(console, "network: expected ping or tcp");
}

static void print_config(
    const struct arwill_console *console,
    const struct arwill_config *config
) {
    if (config == 0) {
        arwill_console_write_line(console, "config: unavailable");
        return;
    }
    arwill_console_write(console, "config.version=");
    write_uint64_decimal(console, config->version);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "remote.enabled=");
    arwill_console_write_line(console, config->remote_enabled ? "true" : "false");
    arwill_console_write(console, "remote.port=");
    write_uint64_decimal(console, config->remote_port);
    arwill_console_write_line(console, "");
    arwill_console_write(console, "remote.key=");
    arwill_console_write_line(
        console, config->remote_key[0] == '\0' ? "(empty)" : "********"
    );
    arwill_console_write(console, "log.level=");
    arwill_console_write_line(
        console, arwill_config_log_level_name(config->log_level)
    );
    arwill_console_write(console, "config.source=");
    arwill_console_write_line(
        console, config->loaded_from_file ? "/owner/arwill.conf" : "defaults"
    );
    arwill_console_write(console, "config.valid=");
    arwill_console_write_line(console, config->valid ? "yes" : "no");
}

static void configure_value(
    const struct arwill_console *console,
    struct arwill_config *config,
    struct arwill_event_log *log,
    const char *argument,
    int *key_requested
) {
    char key[shell_line_capacity];
    char value[shell_line_capacity];
    if (!copy_first_argument(key, sizeof(key), argument)) {
        arwill_console_write_line(console, "config: key too long");
        return;
    }
    if (key[0] == '\0') {
        print_config(console, config);
        return;
    }
    const char *value_argument = second_argument_after_first(argument);
    if (arwill_text_equals(key, "remote.key")) {
        if (value_argument[0] != '\0') {
            arwill_console_write_line(console, "config: remote.key uses hidden input");
            return;
        }
        *key_requested = 1;
        arwill_console_write(console, "new remote key: ");
        return;
    }
    if (!copy_first_argument(value, sizeof(value), value_argument) || value[0] == '\0') {
        arwill_console_write_line(console, "config: expected config <key> <value>");
        return;
    }
    if (!arwill_config_set(config, key, value)) {
        arwill_console_write_line(console, "config: invalid value or write failed");
        return;
    }
    arwill_event_log_record(
        log, arwill_log_info, arwill_log_config, arwill_log_config_changed, 0, 0
    );
    arwill_console_write_line(console, "config: saved");
}

static void print_logs(
    const struct arwill_console *console,
    const struct arwill_event_log *log
) {
    if (log == 0) {
        arwill_console_write_line(console, "logs: unavailable");
        return;
    }
    arwill_console_write_line(console, "ms severity subsystem event arg0 arg1");
    for (size_t index = 0; index < log->count; index++) {
        struct arwill_log_entry entry;
        if (!arwill_event_log_entry(log, index, &entry)) {
            continue;
        }
        write_uint64_decimal(console, entry.milliseconds);
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_log_severity_name(entry.severity));
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_log_subsystem_name(entry.subsystem));
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_log_code_name(entry.code));
        arwill_console_write(console, " ");
        write_uint64_decimal(console, entry.argument0);
        arwill_console_write(console, " ");
        write_uint64_decimal(console, entry.argument1);
        arwill_console_write_line(console, "");
    }
    arwill_console_write(console, "logs overwritten: ");
    write_uint64_decimal(console, log->overwritten);
    arwill_console_write_line(console, "");
}

static void print_service_status(
    const struct arwill_console *console,
    const struct arwill_service_manager *services
) {
    arwill_console_write(console, "remote-console: ");
    if (services == 0) {
        arwill_console_write_line(console, "unavailable");
        return;
    }
    arwill_console_write(
        console, arwill_service_state_name(services->remote_console_state)
    );
    if (services->config != 0) {
        arwill_console_write(console, ", port ");
        write_uint64_decimal(console, services->config->remote_port);
    }
    arwill_console_write_line(console, "");
}

static void control_service(
    const struct arwill_console *console,
    struct arwill_service_manager *services,
    const char *argument,
    int remote_session
) {
    if (arwill_text_equals(argument, "status")) {
        print_service_status(console, services);
        return;
    }
    if (arwill_text_equals(argument, "start remote-console")) {
        if (!arwill_service_remote_console_start(services)) {
            arwill_console_write_line(console, "service: start failed");
            return;
        }
        arwill_console_write_line(console, "service: remote-console running");
        return;
    }
    if (arwill_text_equals(argument, "stop remote-console")) {
        if (remote_session) {
            arwill_console_write_line(console, "service: stopping remote-console");
        }
        if (!arwill_service_remote_console_stop(services)) {
            if (!remote_session) {
                arwill_console_write_line(console, "service: stop failed");
            }
            return;
        }
        if (!remote_session) {
            arwill_console_write_line(console, "service: remote-console stopped");
        }
        return;
    }
    if (arwill_text_equals(argument, "restart remote-console")) {
        if (remote_session) {
            arwill_console_write_line(console, "service: restarting remote-console");
        }
        if (!arwill_service_remote_console_restart(services)) {
            if (!remote_session) {
                arwill_console_write_line(console, "service: restart failed");
            }
            return;
        }
        if (!remote_session) {
            arwill_console_write_line(console, "service: remote-console restarted");
        }
        return;
    }
    arwill_console_write_line(
        console,
        "service: expected status or start/stop/restart remote-console"
    );
}

static struct arwill_process_result shell_counter_process(
    const struct arwill_process_runtime *runtime
) {
    if (runtime == 0 || runtime->context == 0) {
        return arwill_process_finish(1);
    }

    const struct shell_process_context *context =
        (const struct shell_process_context *)runtime->context;

    if (context->console == 0) {
        return arwill_process_finish(1);
    }

    uint64_t value = 10;

    for (uint64_t step = 1; step <= 3U; step++) {
        arwill_console_write(context->console, "process ");
        arwill_console_write(context->console, runtime->name);
        arwill_console_write(context->console, ": pid ");
        write_uint64_decimal(context->console, (uint64_t)runtime->pid);
        arwill_console_write(context->console, " step ");
        write_uint64_decimal(context->console, step);
        arwill_console_write(context->console, "/3 value ");
        write_uint64_decimal(context->console, value);
        arwill_console_write_line(context->console, "");

        value += step;
        if (step < 3U) {
            arwill_process_yield(runtime);
        }
    }

    return arwill_process_finish(0);
}

static struct arwill_process_result shell_user_program_process(
    const struct arwill_process_runtime *runtime,
    enum arwill_user_program program
) {
    if (runtime == 0 || runtime->context == 0) {
        return arwill_process_finish(1);
    }

    const struct shell_process_context *context =
        (const struct shell_process_context *)runtime->context;

    if (context->console == 0 || context->user_runtime == 0) {
        return arwill_process_finish(1);
    }

    struct arwill_user_program_result result;

    if (!arwill_user_run_program(
            context->user_runtime,
            program,
            context->console,
            &result
        )) {
        arwill_console_write(context->console, runtime->name);
        arwill_console_write_line(context->console, ": user program launch failed");
        return arwill_process_finish(1);
    }

    if (!result.exited) {
        arwill_console_write(context->console, runtime->name);
        arwill_console_write_line(context->console, ": user program did not exit");
        return arwill_process_finish(1);
    }

    return arwill_process_finish(result.exit_code);
}

static struct arwill_process_result shell_user_hello_process(
    const struct arwill_process_runtime *runtime
) {
    return shell_user_program_process(runtime, arwill_user_program_hello);
}

static struct arwill_process_result shell_user_bad_process(
    const struct arwill_process_runtime *runtime
) {
    return shell_user_program_process(runtime, arwill_user_program_bad_syscall);
}

static const struct shell_builtin_process shell_builtin_processes[] = {
    { .name = "counter", .entry = shell_counter_process },
    { .name = "userhello", .entry = shell_user_hello_process },
    { .name = "userbad", .entry = shell_user_bad_process },
};

static const struct shell_builtin_process *find_builtin_process(const char *name) {
    const size_t process_count =
        sizeof(shell_builtin_processes) / sizeof(shell_builtin_processes[0]);

    for (size_t index = 0; index < process_count; index++) {
        if (arwill_text_equals(name, shell_builtin_processes[index].name)) {
            return &shell_builtin_processes[index];
        }
    }

    return 0;
}

static void print_available_processes(const struct arwill_console *console) {
    const size_t process_count =
        sizeof(shell_builtin_processes) / sizeof(shell_builtin_processes[0]);

    arwill_console_write(console, "available processes:");

    for (size_t index = 0; index < process_count; index++) {
        arwill_console_write(console, " ");
        arwill_console_write(console, shell_builtin_processes[index].name);
    }

    arwill_console_write_line(console, "");
}

static void print_process_table(
    const struct arwill_console *console,
    const struct arwill_process_manager *processes,
    const struct arwill_user_runtime *user_runtime
) {
    const struct arwill_process *table = arwill_process_table(processes);
    int saw_process = 0;

    if (table == 0) {
        arwill_console_write_line(console, "ps: process manager unavailable");
        return;
    }

    arwill_console_write_line(console, "pid kind state runs exit name");

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        const struct arwill_process *process = &table[index];

        if (process->state == arwill_process_state_empty) {
            continue;
        }

        saw_process = 1;
        write_uint64_decimal(console, (uint64_t)process->pid);
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_process_kind_name(process->kind));
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_process_state_name(process->state));
        arwill_console_write(console, " ");
        write_uint64_decimal(console, process->run_count);
        arwill_console_write(console, " ");
        write_uint64_decimal(console, (uint64_t)process->exit_code);
        arwill_console_write(console, " ");
        arwill_console_write_line(console, process->name);
    }

    if (!saw_process) {
        arwill_console_write_line(console, "no processes");
    }

    struct arwill_user_task_info tasks[arwill_user_task_capacity];
    const size_t task_count = arwill_user_tasks(
        user_runtime, tasks, arwill_user_task_capacity
    );
    if (task_count != 0U) {
        arwill_console_write_line(console, "awp pid state runs exit name");
    }
    for (size_t index = 0; index < task_count; index++) {
        arwill_console_write(console, "awp ");
        write_uint64_decimal(console, tasks[index].pid);
        arwill_console_write(console, " ");
        arwill_console_write(console, arwill_user_task_state_name(tasks[index].state));
        arwill_console_write(console, " ");
        write_uint64_decimal(console, tasks[index].run_count);
        arwill_console_write(console, " ");
        write_uint64_decimal(console, tasks[index].exit_code);
        arwill_console_write(console, " ");
        arwill_console_write_line(console, tasks[index].name);
    }
}

static void print_top_processes(
    const struct arwill_console *console,
    const struct arwill_process_manager *processes,
    const struct arwill_user_runtime *user_runtime
) {
    const struct arwill_process *table = arwill_process_table(processes);

    arwill_console_write_line(console, "PID KIND STATE RUNS EXIT NAME");
    if (table != 0) {
        for (size_t index = 0; index < arwill_process_table_capacity; index++) {
            const struct arwill_process *process = &table[index];
            if (process->state == arwill_process_state_empty) {
                continue;
            }
            write_uint64_decimal(console, process->pid);
            arwill_console_write(console, " ");
            arwill_console_write(
                console, arwill_process_kind_name(process->kind)
            );
            arwill_console_write(console, " ");
            arwill_console_write(console, arwill_process_state_name(process->state));
            arwill_console_write(console, " ");
            write_uint64_decimal(console, process->run_count);
            arwill_console_write(console, " ");
            write_uint64_decimal(console, process->exit_code);
            arwill_console_write(console, " ");
            arwill_console_write_line(console, process->name);
        }
    }

    struct arwill_user_task_info tasks[arwill_user_task_capacity];
    const size_t task_count = arwill_user_tasks(
        user_runtime, tasks, arwill_user_task_capacity
    );
    for (size_t index = 0; index < task_count; index++) {
        write_uint64_decimal(console, tasks[index].pid);
        arwill_console_write(console, " awp ");
        arwill_console_write(console, arwill_user_task_state_name(tasks[index].state));
        arwill_console_write(console, " ");
        write_uint64_decimal(console, tasks[index].run_count);
        arwill_console_write(console, " ");
        write_uint64_decimal(console, tasks[index].exit_code);
        arwill_console_write(console, " ");
        arwill_console_write_line(console, tasks[index].name);
    }
    if (
        process_count_kind(processes, arwill_process_kind_kernel) == 0U &&
        process_count_kind(processes, arwill_process_kind_system) == 0U &&
        task_count == 0U
    ) {
        arwill_console_write_line(console, "no processes");
    }
}

static void render_top(
    const struct shell_session *session,
    const struct shell_environment *environment
) {
    struct arwill_kernel_heap_stats heap;
    struct arwill_scheduler_stats scheduler;
    struct arwill_user_stats user;

    arwill_kernel_heap_stats(environment->memory, &heap);
    arwill_scheduler_stats(&scheduler);
    arwill_user_runtime_stats(environment->user_runtime, &user);

    arwill_console_write(session->console, "\033[2J\033[H");
    arwill_console_write(
        session->console, ARWILL_PROJECT_NAME " " ARWILL_PROJECT_VERSION "  uptime "
    );
    write_uint64_decimal(
        session->console,
        arwill_clock_monotonic_milliseconds(environment->clock)
    );
    arwill_console_write(session->console, " ms  heap ");
    write_size_decimal(session->console, heap.used_bytes);
    arwill_console_write(session->console, "/");
    write_size_decimal(session->console, heap.size_bytes);
    arwill_console_write_line(session->console, " bytes");

    arwill_console_write(session->console, "remote-console ");
    arwill_console_write(
        session->console,
        environment->services == 0 ? "unavailable" :
            arwill_service_state_name(
                environment->services->remote_console_state
            )
    );
    arwill_console_write(session->console, "  tcp ");
    arwill_console_write(
        session->console,
        remote_tcp_state_name(environment->ipv4)
    );
    arwill_console_write(session->console, "  scheduler ticks ");
    write_uint64_decimal(session->console, scheduler.ticks);
    arwill_console_write_line(session->console, "");
    arwill_console_write_line(session->console, "");

    print_top_processes(
        session->console, environment->processes, environment->user_runtime
    );
    arwill_console_write_line(session->console, "");
    arwill_console_write(session->console, "SYSTEM ");
    write_size_decimal(
        session->console,
        process_count_kind(
            environment->processes, arwill_process_kind_system
        )
    );
    arwill_console_write(session->console, " tasks  KERNEL ");
    write_size_decimal(
        session->console,
        process_count_kind(
            environment->processes, arwill_process_kind_kernel
        )
    );
    arwill_console_write(session->console, " tasks  AWP ");
    struct arwill_user_task_info tasks[arwill_user_task_capacity];
    write_size_decimal(
        session->console,
        arwill_user_tasks(
            environment->user_runtime, tasks, arwill_user_task_capacity
        )
    );
    arwill_console_write(session->console, "/");
    write_size_decimal(session->console, arwill_user_task_capacity);
    arwill_console_write(session->console, " slots  faults ");
    write_uint64_decimal(session->console, user.faults);
    arwill_console_write(session->console, "  preemptions ");
    write_uint64_decimal(session->console, user.preemptions);
    arwill_console_write_line(session->console, "");
    arwill_console_write_line(session->console, "q/Ctrl+C exit");
}

static void run_process(
    const struct arwill_console *console,
    struct arwill_process_manager *processes,
    struct shell_process_context *process_context,
    const char *argument
) {
    char process_name[shell_line_capacity];

    if (!copy_first_argument(process_name, sizeof(process_name), argument)) {
        arwill_console_write_line(console, "run: process name too long");
        return;
    }

    if (process_name[0] == '\0') {
        arwill_console_write_line(console, "run: missing process name");
        print_available_processes(console);
        return;
    }

    const struct shell_builtin_process *builtin = find_builtin_process(process_name);

    if (builtin == 0) {
        arwill_console_write(console, "run: unknown process: ");
        arwill_console_write_line(console, process_name);
        print_available_processes(console);
        return;
    }

    uint32_t pid = 0;

    if (!arwill_process_spawn(processes, builtin->name, builtin->entry, process_context, &pid)) {
        arwill_console_write_line(console, "run: process table full");
        return;
    }

    arwill_console_write(console, "run: spawned pid ");
    write_uint64_decimal(console, (uint64_t)pid);
    arwill_console_write(console, ": ");
    arwill_console_write_line(console, builtin->name);

    if (arwill_process_run_ready(processes) == 0U) {
        arwill_console_write_line(console, "run: no ready processes");
    }
}

static void step_processes(
    const struct arwill_console *console,
    struct arwill_process_manager *processes
) {
    const size_t run_count = arwill_process_run_ready(processes);

    if (run_count == 0U) {
        arwill_console_write_line(console, "step: no ready processes");
        return;
    }

    arwill_console_write(console, "step: ran ");
    write_size_decimal(console, run_count);
    arwill_console_write_line(console, " process step(s)");
}

static enum shell_completion_kind command_completion(const char *command) {
    const size_t command_count = sizeof(shell_commands) / sizeof(shell_commands[0]);

    for (size_t index = 0; index < command_count; index++) {
        if (arwill_text_equals(command, shell_commands[index].name)) {
            return shell_commands[index].completion;
        }
    }

    return shell_completion_none;
}

typedef const char *(*shell_candidate_name)(const void *context, size_t index);
typedef int (*shell_candidate_add_space)(const void *context, size_t index);

static const char *command_candidate_name(const void *context, size_t index) {
    const struct shell_command *commands =
        (const struct shell_command *)context;
    return commands[index].name;
}

static int command_candidate_add_space(const void *context, size_t index) {
    const struct shell_command *commands =
        (const struct shell_command *)context;
    return commands[index].completion != shell_completion_none;
}

static const char *process_candidate_name(const void *context, size_t index) {
    const struct shell_builtin_process *processes =
        (const struct shell_builtin_process *)context;
    return processes[index].name;
}

static const char *fixed_candidate_name(const void *context, size_t index) {
    const char *const *candidates = (const char *const *)context;
    return candidates[index];
}

static int always_add_space(const void *context, size_t index) {
    (void)context;
    (void)index;
    return 1;
}

static void complete_text_candidates(
    const struct arwill_console *console,
    const char *current_directory,
    char *line,
    size_t *length,
    size_t prefix_start,
    const void *context,
    size_t candidate_count,
    shell_candidate_name candidate_name,
    shell_candidate_add_space add_space
) {
    const char *prefix = &line[prefix_start];
    const size_t prefix_length = arwill_text_length(prefix);
    const char *single_match = 0;
    size_t single_index = 0;
    size_t match_count = 0;
    size_t shared_length = 0;

    for (size_t index = 0; index < candidate_count; index++) {
        const char *candidate = candidate_name(context, index);

        if (!arwill_text_starts_with_sized(candidate, prefix, prefix_length)) {
            continue;
        }

        if (match_count == 0U) {
            single_match = candidate;
            single_index = index;
            shared_length = arwill_text_length(candidate);
        } else {
            shared_length = common_prefix_length(single_match, candidate, shared_length);
        }

        match_count++;
    }

    if (match_count == 0U) {
        return;
    }

    if (match_count == 1U && single_match != 0) {
        const size_t candidate_length = arwill_text_length(single_match);

        for (size_t index = prefix_length; index < candidate_length; index++) {
            if (!append_char_to_line(console, line, length, single_match[index])) {
                return;
            }
        }

        if (add_space(context, single_index)) {
            (void)append_char_to_line(console, line, length, ' ');
        }

        return;
    }

    if (shared_length > prefix_length) {
        for (size_t index = prefix_length; index < shared_length; index++) {
            if (!append_char_to_line(console, line, length, single_match[index])) {
                return;
            }
        }

        return;
    }

    line[*length] = '\0';
    arwill_console_write_line(console, "");
    for (size_t index = 0; index < candidate_count; index++) {
        const char *candidate = candidate_name(context, index);
        if (arwill_text_starts_with_sized(candidate, prefix, prefix_length)) {
            arwill_console_write_line(console, candidate);
        }
    }
    redraw_line(console, current_directory, line);
}

static void complete_command(
    const struct arwill_console *console,
    const char *current_directory,
    char *line,
    size_t *length
) {
    complete_text_candidates(
        console,
        current_directory,
        line,
        length,
        0U,
        shell_commands,
        sizeof(shell_commands) / sizeof(shell_commands[0]),
        command_candidate_name,
        command_candidate_add_space
    );
}

static void complete_process_name(
    const struct arwill_console *console,
    const char *current_directory,
    char *line,
    size_t *length,
    size_t argument_start
) {
    complete_text_candidates(
        console,
        current_directory,
        line,
        length,
        argument_start,
        shell_builtin_processes,
        sizeof(shell_builtin_processes) / sizeof(shell_builtin_processes[0]),
        process_candidate_name,
        always_add_space
    );
}

static void complete_fixed_argument(
    const struct arwill_console *console,
    const char *current_directory,
    char *line,
    size_t *length,
    size_t argument_start,
    const char *const *candidates,
    size_t candidate_count
) {
    complete_text_candidates(
        console,
        current_directory,
        line,
        length,
        argument_start,
        candidates,
        candidate_count,
        fixed_candidate_name,
        always_add_space
    );
}

static int split_command(
    const char *line,
    char *command,
    size_t command_capacity,
    size_t *argument_start
) {
    size_t command_length = 0;
    size_t index = 0;

    while (line[index] != '\0' && line[index] != ' ') {
        index++;
    }

    command_length = index;

    if (!copy_sized_string(command, command_capacity, line, command_length)) {
        return 0;
    }

    while (line[index] == ' ') {
        index++;
    }

    *argument_start = index;
    return 1;
}

static int two_path_completion_start(
    const char *line,
    size_t first_path_start,
    size_t *path_start
) {
    size_t index = first_path_start;

    while (line[index] != '\0' && line[index] != ' ') {
        index++;
    }
    if (line[index] == '\0') {
        *path_start = first_path_start;
        return 1;
    }

    while (line[index] == ' ') {
        index++;
    }
    const size_t second_path_start = index;
    while (line[index] != '\0' && line[index] != ' ') {
        index++;
    }
    if (line[index] != '\0') {
        return 0;
    }

    *path_start = second_path_start;
    return 1;
}

static void split_path_for_completion(
    const char *path,
    char *parent_path,
    size_t parent_capacity,
    char *prefix,
    size_t prefix_capacity
) {
    size_t last_slash = 0;
    size_t length = arwill_text_length(path);
    int saw_slash = 0;

    for (size_t index = 0; index < length; index++) {
        if (path[index] == '/') {
            last_slash = index;
            saw_slash = 1;
        }
    }

    if (!saw_slash) {
        (void)copy_string(parent_path, parent_capacity, ".");
        (void)copy_sized_string(prefix, prefix_capacity, path, length);
        return;
    }

    if (last_slash == 0U) {
        (void)copy_string(parent_path, parent_capacity, "/");
    } else {
        (void)copy_sized_string(parent_path, parent_capacity, path, last_slash);
    }

    (void)copy_sized_string(
        prefix,
        prefix_capacity,
        &path[last_slash + 1U],
        length - last_slash - 1U
    );
}

static void show_path_candidates(
    const struct arwill_console *console,
    const char *current_directory,
    const char *line,
    const struct arwill_fs_listing *listing,
    const char *prefix,
    int directories_only
) {
    arwill_console_write_line(console, "");

    for (size_t index = 0; index < listing->count; index++) {
        const struct arwill_fs_entry *entry = &listing->entries[index];

        if (directories_only && entry->type != arwill_fs_entry_directory) {
            continue;
        }

        if (!arwill_text_starts_with_sized(entry->name, prefix, arwill_text_length(prefix))) {
            continue;
        }

        arwill_console_write(console, entry->name);
        if (entry->type == arwill_fs_entry_directory) {
            arwill_console_write(console, "/");
        }
        arwill_console_write_line(console, "");
    }

    redraw_line(console, current_directory, line);
}

static void complete_path(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    char *line,
    size_t *length,
    size_t argument_start,
    int directories_only
) {
    char path_argument[shell_path_capacity];
    char parent_input[shell_path_capacity];
    char parent_path[shell_path_capacity];
    char prefix[shell_path_capacity];
    const struct arwill_fs_entry *single_match = 0;
    size_t match_count = 0;
    size_t shared_length = 0;

    if (!copy_string(path_argument, sizeof(path_argument), &line[argument_start])) {
        return;
    }

    split_path_for_completion(
        path_argument,
        parent_input,
        sizeof(parent_input),
        prefix,
        sizeof(prefix)
    );

    if (!resolve_path(current_directory, parent_input, parent_path, sizeof(parent_path))) {
        return;
    }

    struct arwill_fs_listing listing;

    if (!arwill_filesystem_list(filesystem, parent_path, &listing)) {
        return;
    }

    const size_t prefix_length = arwill_text_length(prefix);

    for (size_t index = 0; index < listing.count; index++) {
        const struct arwill_fs_entry *entry = &listing.entries[index];

        if (directories_only && entry->type != arwill_fs_entry_directory) {
            continue;
        }

        if (!arwill_text_starts_with_sized(entry->name, prefix, prefix_length)) {
            continue;
        }

        if (match_count == 0U) {
            single_match = entry;
            shared_length = arwill_text_length(entry->name);
        } else if (single_match != 0) {
            shared_length = common_prefix_length(
                single_match->name,
                entry->name,
                shared_length
            );
        }

        match_count++;
    }

    if (match_count == 0U) {
        return;
    }

    if (match_count == 1U && single_match != 0) {
        const size_t match_length = arwill_text_length(single_match->name);

        for (size_t index = prefix_length; index < match_length; index++) {
            if (!append_char_to_line(console, line, length, single_match->name[index])) {
                return;
            }
        }

        if (single_match->type == arwill_fs_entry_directory) {
            (void)append_char_to_line(console, line, length, '/');
        } else {
            (void)append_char_to_line(console, line, length, ' ');
        }

        return;
    }

    if (shared_length > prefix_length && single_match != 0) {
        for (size_t index = prefix_length; index < shared_length; index++) {
            if (!append_char_to_line(console, line, length, single_match->name[index])) {
                return;
            }
        }

        return;
    }

    line[*length] = '\0';
    show_path_candidates(console, current_directory, line, &listing, prefix, directories_only);
}

static void show_program_candidates(
    const struct arwill_console *console,
    const char *current_directory,
    const char *line,
    const struct arwill_fs_listing *listing,
    const char *prefix
) {
    static const char program_suffix[] = ".awp";
    const size_t suffix_length = sizeof(program_suffix) - 1U;
    const size_t prefix_length = arwill_text_length(prefix);
    char program_name[shell_path_capacity];

    arwill_console_write_line(console, "");
    for (size_t index = 0; index < listing->count; index++) {
        const struct arwill_fs_entry *entry = &listing->entries[index];
        const size_t name_length = arwill_text_length(entry->name);

        if (entry->type != arwill_fs_entry_file ||
            !string_ends_with(entry->name, program_suffix) ||
            name_length <= suffix_length ||
            !arwill_text_starts_with_sized(entry->name, prefix, prefix_length)) {
            continue;
        }
        if (copy_sized_string(
                program_name,
                sizeof(program_name),
                entry->name,
                name_length - suffix_length
            )) {
            arwill_console_write_line(console, program_name);
        }
    }
    redraw_line(console, current_directory, line);
}

static void complete_program_name(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    char *line,
    size_t *length,
    size_t argument_start
) {
    static const char program_suffix[] = ".awp";
    const size_t suffix_length = sizeof(program_suffix) - 1U;
    const char *prefix = &line[argument_start];
    const size_t prefix_length = arwill_text_length(prefix);
    const struct arwill_fs_entry *single_match = 0;
    size_t match_count = 0;
    size_t shared_length = 0;
    struct arwill_fs_listing listing;

    if (string_contains(prefix, '/') || string_contains(prefix, '.')) {
        complete_path(
            console,
            filesystem,
            current_directory,
            line,
            length,
            argument_start,
            0
        );
        return;
    }

    if (!arwill_filesystem_list(filesystem, "/apps", &listing)) {
        return;
    }

    for (size_t index = 0; index < listing.count; index++) {
        const struct arwill_fs_entry *entry = &listing.entries[index];
        const size_t name_length = arwill_text_length(entry->name);

        if (entry->type != arwill_fs_entry_file ||
            !string_ends_with(entry->name, program_suffix) ||
            name_length <= suffix_length ||
            !arwill_text_starts_with_sized(entry->name, prefix, prefix_length)) {
            continue;
        }

        const size_t program_length = name_length - suffix_length;
        if (match_count == 0U) {
            single_match = entry;
            shared_length = program_length;
        } else if (single_match != 0) {
            shared_length = common_prefix_length(
                single_match->name,
                entry->name,
                shared_length < program_length ? shared_length : program_length
            );
        }
        match_count++;
    }

    if (match_count == 0U) {
        return;
    }
    if (match_count == 1U && single_match != 0) {
        const size_t program_length = arwill_text_length(single_match->name) - suffix_length;
        for (size_t index = prefix_length; index < program_length; index++) {
            if (!append_char_to_line(console, line, length, single_match->name[index])) {
                return;
            }
        }
        (void)append_char_to_line(console, line, length, ' ');
        return;
    }
    if (shared_length > prefix_length && single_match != 0) {
        for (size_t index = prefix_length; index < shared_length; index++) {
            if (!append_char_to_line(console, line, length, single_match->name[index])) {
                return;
            }
        }
        return;
    }

    line[*length] = '\0';
    show_program_candidates(
        console,
        current_directory,
        line,
        &listing,
        prefix
    );
}

static void complete_line(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const char *current_directory,
    char *line,
    size_t *length
) {
    char command[shell_line_capacity];
    size_t argument_start = 0;

    line[*length] = '\0';

    if (!split_command(line, command, sizeof(command), &argument_start)) {
        return;
    }

    if (arwill_text_length(command) == *length) {
        complete_command(console, current_directory, line, length);
        return;
    }

    const enum shell_completion_kind completion = command_completion(command);

    if (completion == shell_completion_none) {
        return;
    }

    if (completion == shell_completion_process) {
        complete_process_name(console, current_directory, line, length, argument_start);
        return;
    }

    if (completion == shell_completion_system) {
        complete_fixed_argument(
            console,
            current_directory,
            line,
            length,
            argument_start,
            system_completions,
            sizeof(system_completions) / sizeof(system_completions[0])
        );
        return;
    }

    if (completion == shell_completion_devices) {
        complete_fixed_argument(
            console,
            current_directory,
            line,
            length,
            argument_start,
            device_completions,
            sizeof(device_completions) / sizeof(device_completions[0])
        );
        return;
    }

    if (completion == shell_completion_network) {
        complete_fixed_argument(
            console,
            current_directory,
            line,
            length,
            argument_start,
            network_completions,
            sizeof(network_completions) / sizeof(network_completions[0])
        );
        return;
    }

    if (completion == shell_completion_exec) {
        const size_t first_argument_start = argument_start;

        if (!two_path_completion_start(line, argument_start, &argument_start)) {
            return;
        }
        if (argument_start == first_argument_start) {
            complete_program_name(
                console,
                filesystem,
                current_directory,
                line,
                length,
                argument_start
            );
            return;
        }
    }

    complete_path(
        console,
        filesystem,
        current_directory,
        line,
        length,
        argument_start,
        completion == shell_completion_directory_path
    );
}

static void run_command(
    struct shell_session *session,
    const struct shell_environment *environment,
    const char *line,
    int *top_requested,
    int *close_requested
) {
    const struct arwill_console *console = session->console;
    const struct arwill_filesystem *filesystem = environment->filesystem;
    struct arwill_memory *memory = environment->memory;
    const struct arwill_power *power = environment->power;
    struct arwill_process_manager *processes = environment->processes;
    const struct arwill_pci_bus *pci = environment->pci;
    const struct arwill_network_device *network = environment->network;
    struct arwill_ipv4_stack *ipv4 = environment->ipv4;
    const struct arwill_block_device *block_device = environment->block_device;
    const struct arwill_interrupts *interrupts = environment->interrupts;
    const struct arwill_clock *clock = environment->clock;
    const struct arwill_user_runtime *user_runtime = environment->user_runtime;
    const struct arwill_device_registry *devices = environment->devices;
    struct arwill_config *config = environment->config;
    struct arwill_event_log *log = environment->log;
    struct arwill_service_manager *services = environment->services;
    struct shell_process_context *process_context = &session->process_context;
    char *current_directory = session->current_directory;
    const int remote_session = session->remote;
    uint32_t *foreground_pid = &session->foreground_pid;
    int *config_key_requested = &session->config_key_pending;
    char command[shell_line_capacity];
    size_t argument_start = 0;
    if (!split_command(line, command, sizeof(command), &argument_start) ||
        command[0] == '\0') {
        return;
    }
    const char *argument = &line[argument_start];

    if (arwill_text_equals(line, "help")) {
        print_help(console, remote_session);
        return;
    }

    if (arwill_text_equals(line, "version")) {
        print_version(console);
        return;
    }

    if (arwill_text_equals(command, "system")) {
        run_system_command(
            console,
            argument,
            clock,
            filesystem,
            memory,
            interrupts,
            processes,
            user_runtime,
            services
        );
        return;
    }

    if (arwill_text_equals(command, "devices")) {
        run_devices_command(
            console,
            argument,
            devices,
            pci,
            block_device,
            network
        );
        return;
    }

    if (arwill_text_equals(command, "network")) {
        run_network_command(console, argument, network, ipv4);
        return;
    }

    if (arwill_text_equals(line, "top")) {
        *top_requested = 1;
        return;
    }

    if (arwill_text_equals(line, "netprobe")) {
        uint8_t mac[arwill_network_mac_length];
        uint8_t frame[60];
        static const char payload[] = "ARWILL-NETWORK-FRAME-TEST";
        const size_t payload_offset = 14U;

        if (network == 0 || !arwill_network_read_mac(network, mac)) {
            arwill_console_write_line(console, "netprobe: network unavailable");
            return;
        }
        for (size_t index = 0; index < sizeof(frame); index++) {
            frame[index] = 0;
        }
        for (size_t index = 0; index < arwill_network_mac_length; index++) {
            frame[index] = 0xffU;
            frame[index + arwill_network_mac_length] = mac[index];
        }
        frame[12] = 0x88U;
        frame[13] = 0xb5U;
        for (size_t index = 0; index + 1U < sizeof(payload); index++) {
            frame[payload_offset + index] = (uint8_t)payload[index];
        }
        if (!arwill_network_send_frame(network, frame, sizeof(frame))) {
            arwill_console_write_line(console, "netprobe: transmit failed");
            return;
        }
        arwill_console_write(console, "netprobe: transmitted ");
        write_size_decimal(console, sizeof(frame));
        arwill_console_write_line(console, " bytes");
        return;
    }

    if (arwill_text_equals(line, "arping")) {
        if (ipv4 == 0 || !arwill_ipv4_send_arp_request(ipv4, ipv4->gateway)) {
            arwill_console_write_line(console, "arping: transmit failed");
            return;
        }
        arwill_console_write_line(console, "arping: request transmitted to 10.0.2.2");
        return;
    }

    if (arwill_text_equals(line, "tcpcheck")) {
        struct arwill_tcp_listener listener;
        struct arwill_tcp_segment syn = { 0 };
        struct arwill_tcp_segment reply = { 0 };
        struct arwill_tcp_segment ack = { 0 };
        struct arwill_tcp_endpoint_snapshot remote;
        const uint16_t listener_port =
            arwill_ipv4_tcp_endpoint_snapshot(ipv4, 0U, &remote)
                ? remote.local_port : 23232U;

        arwill_tcp_listener_init(&listener, listener_port, 0x41520000U);
        syn.source_port = 4242U;
        syn.destination_port = listener_port;
        syn.sequence = 100U;
        syn.acknowledgement = 0U;
        syn.flags = arwill_tcp_flag_syn;
        syn.payload_length = 0U;
        if (!arwill_tcp_listener_receive(&listener, &syn, &reply) ||
            reply.flags != (arwill_tcp_flag_syn | arwill_tcp_flag_ack) ||
            reply.acknowledgement != 101U) {
            arwill_console_write_line(console, "tcpcheck: SYN/SYN-ACK failed");
            return;
        }
        ack.source_port = syn.source_port;
        ack.destination_port = listener_port;
        ack.sequence = 101U;
        ack.acknowledgement = reply.sequence + 1U;
        ack.flags = arwill_tcp_flag_ack;
        ack.payload_length = 0U;
        if (!arwill_tcp_listener_receive(&listener, &ack, &reply)) {
            arwill_console_write_line(console, "tcpcheck: ACK failed");
            return;
        }
        arwill_console_write(console, "tcpcheck: listener state ");
        arwill_console_write_line(console, arwill_tcp_state_name(listener.state));
        return;
    }

    if (arwill_text_equals(line, "tcplisten")) {
        size_t processed = 0;
        if (ipv4 == 0 || !arwill_ipv4_service_tcp(ipv4, &processed)) {
            arwill_console_write_line(console, "tcplisten: network unavailable");
            return;
        }
        arwill_console_write(console, "tcplisten: frames ");
        write_size_decimal(console, processed);
        arwill_console_write(console, ", state ");
        arwill_console_write_line(console, remote_tcp_state_name(ipv4));
        arwill_console_write(console, "tcp frames: ");
        write_uint64_decimal(console, ipv4->tcp_frames_received);
        arwill_console_write(console, ", syn-ack: ");
        write_uint64_decimal(console, ipv4->tcp_syn_ack_sent);
        arwill_console_write_line(console, "");
        print_remote_console_info(console, ipv4);
        return;
    }

    if (arwill_text_equals(line, "pwd")) {
        arwill_console_write_line(console, current_directory);
        return;
    }

    if (arwill_text_equals(line, "clear")) {
        clear_screen(console);
        return;
    }

    if (arwill_text_equals(line, "heaptest")) {
        run_heap_test(console, memory);
        return;
    }

    if (arwill_text_equals(line, "irqprobe")) {
        probe_interrupts(console, interrupts);
        return;
    }

    if (arwill_text_equals(command, "config")) {
        configure_value(
            console, config, log, argument, config_key_requested
        );
        return;
    }

    if (arwill_text_equals(line, "logs")) {
        print_logs(console, log);
        return;
    }

    if (arwill_text_equals(command, "service")) {
        control_service(console, services, argument, remote_session);
        return;
    }

    if (arwill_text_equals(line, "ps")) {
        print_process_table(console, processes, user_runtime);
        return;
    }

    if (arwill_text_equals(command, "run")) {
        run_process(console, processes, process_context, argument);
        return;
    }

    if (arwill_text_equals(command, "exec")) {
        *foreground_pid = exec_program_image(
            console,
            filesystem,
            user_runtime,
            current_directory,
            argument
        );
        if (*foreground_pid != 0U) {
            arwill_event_log_record(
                log, arwill_log_info, arwill_log_awp, arwill_log_awp_started,
                *foreground_pid, remote_session != 0
            );
        }
        return;
    }

    if (arwill_text_equals(line, "step")) {
        step_processes(console, processes);
        return;
    }

    if (arwill_text_equals(line, "exit")) {
        if (remote_session) {
            arwill_console_write_line(console, "remote console: disconnected");
            *close_requested = 1;
            return;
        }
        arwill_console_write_line(console, "status: powering off");
        arwill_poweroff(power);
    }

    if (arwill_text_equals(command, "cd")) {
        change_directory(console, filesystem, current_directory, argument);
        return;
    }

    if (arwill_text_equals(line, "halt")) {
        arwill_console_write_line(console, "status: shell halted");
        arwill_cpu_idle_forever();
    }

    if (arwill_text_equals(command, "ls")) {
        print_listing(console, filesystem, current_directory, argument);
        return;
    }

    if (arwill_text_equals(command, "cat")) {
        print_file(console, filesystem, current_directory, argument);
        return;
    }

    if (arwill_text_equals(command, "mkdir")) {
        mutate_path(console, filesystem, current_directory,
            argument, "mkdir", 1);
        return;
    }

    if (arwill_text_equals(command, "write")) {
        write_file(console, filesystem, current_directory, argument);
        return;
    }

    if (arwill_text_equals(command, "writehex")) {
        write_hex_file(console, filesystem, current_directory, argument);
        return;
    }

    if (arwill_text_equals(command, "rm")) {
        mutate_path(console, filesystem, current_directory,
            argument, "rm", 0);
        return;
    }

    if (arwill_text_equals(command, "stat")) {
        print_stat(
            console,
            filesystem,
            current_directory,
            argument,
            "stat"
        );
        return;
    }

    arwill_console_write(console, "unknown command: ");
    arwill_console_write_line(console, line);
}

static void reset_shell_input(struct shell_session *session) {
    session->length = 0;
    session->cursor = 0;
    session->escape_state = shell_escape_none;
    session->normalizer.utf8_state = shell_utf8_none;
    session->normalizer.utf8_lead = 0;
    session->normalizer.russian_layout_active = 0;
}

static void initialize_shell_session(
    struct shell_session *session,
    const struct arwill_console *console,
    const struct arwill_user_runtime *user_runtime,
    int remote
) {
    session->console = console;
    session->current_directory[0] = '/';
    session->current_directory[1] = '\0';
    session->history.count = 0;
    session->history_position = 0;
    session->remote = remote;
    session->active = 1;
    session->ignore_line_feed = 0;
    session->foreground_pid = 0;
    session->config_key_pending = 0;
    session->config_key_length = 0;
    session->authenticated = remote ? 0 : 1;
    session->authentication_attempts = 0;
    session->authentication_started_milliseconds = 0;
    session->authentication_key_length = 0;
    session->tcp_timeouts_at_connection = 0;
    session->top_active = 0;
    session->top_last_refresh_milliseconds = 0;
    session->process_context.console = console;
    session->process_context.user_runtime = user_runtime;
    reset_shell_input(session);
}

static int handle_shell_byte(
    struct shell_session *session,
    const struct shell_environment *environment,
    uint8_t byte
) {
    const struct arwill_console *console = session->console;

    if (session->config_key_pending) {
        if (byte == ascii_interrupt) {
            arwill_console_write_line(console, "^C");
            session->config_key_pending = 0;
            session->config_key_length = 0;
            write_prompt(console, session->current_directory);
            return 1;
        }
        if (byte == ascii_carriage_return || byte == ascii_line_feed) {
            session->config_key[session->config_key_length] = '\0';
            arwill_console_write_line(console, "");
            if (arwill_config_set_remote_key(
                    environment->config, session->config_key
                )) {
                arwill_event_log_record(
                    environment->log, arwill_log_info, arwill_log_config,
                    arwill_log_config_changed, 0, 0
                );
                arwill_console_write_line(console, "config: saved");
            } else {
                arwill_console_write_line(console, "config: invalid key or write failed");
            }
            session->config_key_pending = 0;
            session->config_key_length = 0;
            write_prompt(console, session->current_directory);
            return 1;
        }
        if (byte == ascii_backspace || byte == ascii_delete) {
            if (session->config_key_length != 0U) {
                session->config_key_length--;
            }
            return 1;
        }
        if (is_printable_ascii(byte) && byte != '=' &&
            session->config_key_length + 1U < sizeof(session->config_key)) {
            session->config_key[session->config_key_length++] = (char)byte;
        }
        return 1;
    }

    if (session->top_active) {
        if (byte == ascii_interrupt || byte == (uint8_t)'q') {
            session->top_active = 0;
            arwill_console_write(console, "\033[2J\033[H");
            reset_shell_input(session);
            write_prompt(console, session->current_directory);
        }
        return 1;
    }

    if (session->foreground_pid != 0U) {
        if (byte == ascii_interrupt) {
            (void)arwill_user_cancel(
                environment->user_runtime, session->foreground_pid, 130U
            );
            arwill_console_write_line(console, "^C");
        } else {
            (void)arwill_user_deliver_input(
                environment->user_runtime, session->foreground_pid, byte
            );
        }
        return 1;
    }

    if (byte == ascii_line_feed && session->ignore_line_feed) {
        session->ignore_line_feed = 0;
        return 1;
    }
    session->ignore_line_feed = 0;

    if (byte == ascii_interrupt) {
        arwill_console_write_line(console, "^C");
        reset_shell_input(session);
        session->history_position = session->history.count;
        write_prompt(console, session->current_directory);
        return 1;
    }

    if (session->escape_state == shell_escape_started) {
        if (byte == ascii_left_bracket || byte == (uint8_t)'O') {
            session->escape_state = shell_escape_bracket;
        } else {
            session->escape_state = shell_escape_none;
        }
        return 1;
    }

    if (session->escape_state == shell_escape_bracket) {
        if (byte == ascii_arrow_up) {
            history_previous(
                console,
                &session->history,
                session->line,
                &session->length,
                &session->cursor,
                &session->history_position
            );
        } else if (byte == ascii_arrow_down) {
            history_next(
                console,
                &session->history,
                session->line,
                &session->length,
                &session->cursor,
                &session->history_position
            );
        } else if (byte == ascii_arrow_right && session->cursor < session->length) {
            move_console_cursor_right(console, 1U);
            session->cursor++;
        } else if (byte == ascii_arrow_left && session->cursor != 0U) {
            move_console_cursor_left(console, 1U);
            session->cursor--;
        }
        session->escape_state = shell_escape_none;
        return 1;
    }

    if (byte == ascii_escape) {
        session->escape_state = shell_escape_started;
        return 1;
    }

    if (byte == ascii_carriage_return || byte == ascii_line_feed) {
        if (session->remote && byte == ascii_carriage_return) {
            session->ignore_line_feed = 1;
        }
        session->line[session->length] = '\0';
        arwill_console_write_line(console, "");
        history_add(&session->history, session->line);
        session->history_position = session->history.count;
        int close_requested = 0;
        int top_requested = 0;
        run_command(
            session,
            environment,
            session->line,
            &top_requested,
            &close_requested
        );
        reset_shell_input(session);
        if (close_requested) {
            return 0;
        }
        if (top_requested) {
            session->top_active = 1;
            session->top_last_refresh_milliseconds =
                arwill_clock_monotonic_milliseconds(environment->clock);
            render_top(session, environment);
        }
        if (session->foreground_pid == 0U && !session->config_key_pending &&
            !session->top_active) {
            write_prompt(console, session->current_directory);
        }
        return 1;
    }

    if (byte == ascii_backspace || byte == ascii_delete) {
        if (session->cursor != 0U) {
            const size_t removed = session->cursor - 1U;
            for (size_t index = removed; index < session->length; index++) {
                session->line[index] = session->line[index + 1U];
            }
            session->length--;
            session->cursor--;
            arwill_console_write(console, "\b");
            for (size_t index = session->cursor; index < session->length; index++) {
                write_byte_echo(console, (uint8_t)session->line[index]);
            }
            arwill_console_write(console, " ");
            move_console_cursor_left(
                console, session->length - session->cursor + 1U
            );
            if (session->length == 0U) {
                session->normalizer.russian_layout_active = 0;
            }
        }
        return 1;
    }

    if (byte == ascii_tab) {
        if (session->cursor != session->length) {
            return 1;
        }
        complete_line(
            console,
            environment->filesystem,
            session->current_directory,
            session->line,
            &session->length
        );
        session->cursor = session->length;
        return 1;
    }

    char input_char;
    if (!normalize_text_input_byte(&session->normalizer, byte, &input_char)) {
        return 1;
    }

    session->history_position = session->history.count;
    (void)insert_char_into_line(
        console,
        session->line,
        &session->length,
        &session->cursor,
        input_char
    );
    return 1;
}

static uint64_t remote_peer_address(const struct arwill_tcp_stream *stream) {
    if (stream == 0) {
        return 0;
    }
    const uint8_t *address = arwill_tcp_stream_peer_address(stream);
    return ((uint64_t)address[0] << 24U) |
        ((uint64_t)address[1] << 16U) |
        ((uint64_t)address[2] << 8U) |
        (uint64_t)address[3];
}

static void record_remote_event(
    const struct shell_environment *environment,
    enum arwill_log_severity severity,
    enum arwill_log_subsystem subsystem,
    enum arwill_log_code code,
    uint64_t detail
) {
    arwill_event_log_record(
        environment->log,
        severity,
        subsystem,
        code,
        remote_peer_address(environment->remote_stream),
        detail
    );
}

static int constant_time_key_matches(
    const struct arwill_config *config,
    const char *candidate,
    size_t candidate_length
) {
    if (config == 0 || candidate == 0 || config->remote_key[0] == '\0') {
        return 0;
    }
    volatile uint8_t difference = 0;
    size_t configured_length = 0;
    for (size_t index = 0; index < arwill_config_remote_key_capacity; index++) {
        const uint8_t expected = (uint8_t)config->remote_key[index];
        const uint8_t received = index < candidate_length
            ? (uint8_t)candidate[index] : 0U;
        difference = (uint8_t)(difference | (uint8_t)(expected ^ received));
        if (expected != 0U) {
            configured_length++;
        }
    }
    size_t length_difference = configured_length ^ candidate_length;
    for (size_t index = 0; index < sizeof(length_difference); index++) {
        difference = (uint8_t)(difference | (uint8_t)length_difference);
        length_difference >>= 8U;
    }
    return difference == 0U;
}

static int handle_authentication_byte(
    struct shell_session *session,
    const struct shell_environment *environment,
    uint8_t byte
) {
    if (byte == ascii_line_feed && session->ignore_line_feed) {
        session->ignore_line_feed = 0;
        return 1;
    }
    session->ignore_line_feed = 0;
    if (byte == ascii_carriage_return || byte == ascii_line_feed) {
        if (byte == ascii_carriage_return) {
            session->ignore_line_feed = 1;
        }
        session->authentication_key[session->authentication_key_length] = '\0';
        if (constant_time_key_matches(
                environment->config,
                session->authentication_key,
                session->authentication_key_length
            )) {
            session->authenticated = 1;
            record_remote_event(
                environment, arwill_log_info, arwill_log_auth,
                arwill_log_auth_accepted,
                (uint64_t)(session->authentication_attempts + 1U)
            );
            session->authentication_key_length = 0;
            arwill_console_write_line(session->console, "");
            arwill_console_write_line(session->console, "Arwill remote console");
            arwill_console_write_line(
                session->console,
                "warning: plaintext trusted-LAN access"
            );
            write_prompt(session->console, session->current_directory);
            return 1;
        }
        session->authentication_attempts++;
        record_remote_event(
            environment, arwill_log_warning, arwill_log_auth,
            arwill_log_auth_rejected,
            session->authentication_attempts
        );
        session->authentication_key_length = 0;
        arwill_console_write_line(session->console, "");
        if (session->authentication_attempts >= remote_authentication_max_attempts) {
            arwill_console_write_line(session->console, "Access denied");
            return 0;
        }
        arwill_console_write_line(session->console, "Access denied");
        arwill_console_write(session->console, "Access key: ");
        return 1;
    }
    if (byte == ascii_backspace || byte == ascii_delete) {
        if (session->authentication_key_length != 0U) {
            session->authentication_key_length--;
        }
        return 1;
    }
    if (is_printable_ascii(byte) &&
        session->authentication_key_length + 1U <
            sizeof(session->authentication_key)) {
        session->authentication_key[session->authentication_key_length++] =
            (char)byte;
    }
    return 1;
}

static void cancel_remote_program(
    struct shell_session *session,
    const struct shell_environment *environment
) {
    if (session->foreground_pid != 0U) {
        (void)arwill_user_cancel(
            environment->user_runtime, session->foreground_pid, 130U
        );
        session->foreground_pid = 0;
    }
}

static void close_remote_session(
    struct shell_session *session,
    const struct shell_environment *environment
) {
    cancel_remote_program(session, environment);
    session->top_active = 0;
    record_remote_event(
        environment, arwill_log_info, arwill_log_network,
        arwill_log_tcp_disconnected,
        arwill_tcp_stream_peer_port(environment->remote_stream)
    );
    arwill_tcp_stream_close(environment->remote_stream);
    session->active = 0;
}

static void service_top(
    struct shell_session *session,
    const struct shell_environment *environment
) {
    if (session == 0 || !session->active || !session->top_active) {
        return;
    }
    const uint64_t now = arwill_clock_monotonic_milliseconds(environment->clock);
    if (now - session->top_last_refresh_milliseconds < top_refresh_interval_ms) {
        return;
    }
    session->top_last_refresh_milliseconds = now;
    render_top(session, environment);
}

static void service_remote_shell(
    struct shell_session *session,
    const struct shell_environment *environment
) {
    struct arwill_ipv4_stack *ipv4 = environment->ipv4;
    struct arwill_tcp_stream *stream = environment->remote_stream;
    if (ipv4 == 0 || stream == 0) {
        return;
    }

    if (arwill_tcp_stream_close_requested(stream)) {
        return;
    }

    if (!arwill_tcp_stream_connected(stream)) {
        if (session->active) {
            cancel_remote_program(session, environment);
            if (ipv4->tcp_timeouts > session->tcp_timeouts_at_connection) {
                record_remote_event(
                    environment, arwill_log_warning, arwill_log_network,
                    arwill_log_tcp_timeout, ipv4->tcp_timeouts
                );
            }
            record_remote_event(
                environment, arwill_log_info, arwill_log_network,
                arwill_log_tcp_disconnected,
                arwill_tcp_stream_peer_port(stream)
            );
        }
        session->active = 0;
        return;
    }

    if (!session->active) {
        initialize_shell_session(
            session,
            session->console,
            environment->user_runtime,
            1
        );
        session->authentication_started_milliseconds =
            arwill_clock_monotonic_milliseconds(environment->clock);
        session->tcp_timeouts_at_connection = ipv4->tcp_timeouts;
        record_remote_event(
            environment, arwill_log_info, arwill_log_network,
            arwill_log_tcp_connected,
            arwill_tcp_stream_peer_port(stream)
        );
        arwill_console_write(session->console, "Access key: ");
    }

    if (!session->authenticated &&
        arwill_clock_monotonic_milliseconds(environment->clock) -
            session->authentication_started_milliseconds >=
                remote_authentication_timeout_ms) {
        record_remote_event(
            environment, arwill_log_warning, arwill_log_network,
            arwill_log_tcp_timeout, remote_authentication_timeout_ms
        );
        close_remote_session(session, environment);
        return;
    }

    uint8_t byte = 0;
    const size_t bytes_read = arwill_tcp_stream_read(stream, &byte, 1U);
    if (bytes_read == 1U) {
        const int keep_open = session->authenticated
            ? handle_shell_byte(session, environment, byte)
            : handle_authentication_byte(session, environment, byte);
        if (!keep_open) {
            close_remote_session(session, environment);
            return;
        }
    }

    if (bytes_read == 0U && arwill_tcp_stream_peer_closed(stream)) {
        close_remote_session(session, environment);
    }
}

struct shell_system_task_context {
    struct arwill_ipv4_stack *ipv4;
    struct shell_session *remote_session;
    const struct shell_environment *environment;
};

static struct arwill_process_result network_poll_system_task(
    const struct arwill_process_runtime *runtime
) {
    if (runtime == 0 || runtime->context == 0) {
        return arwill_process_finish(1);
    }

    struct shell_system_task_context *context =
        (struct shell_system_task_context *)runtime->context;
    if (context->ipv4 == 0) {
        return arwill_process_finish(1);
    }

    for (;;) {
        (void)arwill_ipv4_poll_tcp(context->ipv4);
        arwill_process_yield(runtime);
    }
}

static struct arwill_process_result remote_console_system_task(
    const struct arwill_process_runtime *runtime
) {
    if (runtime == 0 || runtime->context == 0) {
        return arwill_process_finish(1);
    }

    struct shell_system_task_context *context =
        (struct shell_system_task_context *)runtime->context;
    if (context->remote_session == 0 || context->environment == 0) {
        return arwill_process_finish(1);
    }

    for (;;) {
        service_remote_shell(context->remote_session, context->environment);
        arwill_process_yield(runtime);
    }
}

static int start_shell_system_tasks(
    struct arwill_process_manager *processes,
    struct shell_system_task_context *context
) {
    uint32_t network_pid = 0;
    uint32_t remote_console_pid = 0;

    return arwill_process_spawn_system(
        processes,
        "network-poll",
        network_poll_system_task,
        context,
        &network_pid
    ) && arwill_process_spawn_system(
        processes,
        "remote-console",
        remote_console_system_task,
        context,
        &remote_console_pid
    );
}

static void finish_foreground_program(
    struct shell_session *session,
    const struct arwill_user_runtime *user_runtime,
    struct arwill_event_log *log
) {
    if (session == 0 || session->foreground_pid == 0U) {
        return;
    }
    struct arwill_user_task_info task;
    if (!arwill_user_task_info(user_runtime, session->foreground_pid, &task) ||
        (task.state != arwill_user_task_finished &&
         task.state != arwill_user_task_faulted)) {
        return;
    }
    if (task.state == arwill_user_task_faulted) {
        arwill_console_write(session->console, "exec: fault ");
        write_uint64_decimal(session->console, task.fault_vector);
        arwill_console_write_line(session->console, "");
    }
    arwill_event_log_record(
        log,
        task.state == arwill_user_task_faulted ? arwill_log_error : arwill_log_info,
        arwill_log_awp,
        task.state == arwill_user_task_faulted
            ? arwill_log_awp_faulted : arwill_log_awp_exited,
        task.pid,
        task.state == arwill_user_task_faulted ? task.fault_vector : task.exit_code
    );
    arwill_console_write(session->console, "exec: exited ");
    write_uint64_decimal(session->console, task.exit_code);
    arwill_console_write_line(session->console, "");
    session->foreground_pid = 0;
    if (session->active) {
        write_prompt(session->console, session->current_directory);
    }
}

void arwill_shell_run(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    struct arwill_memory *memory,
    const struct arwill_power *power,
    struct arwill_process_manager *processes,
    const struct arwill_pci_bus *pci,
    const struct arwill_network_device *network,
    struct arwill_ipv4_stack *ipv4,
    const struct arwill_block_device *block_device,
    const struct arwill_interrupts *interrupts,
    const struct arwill_clock *clock,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_device_registry *devices,
    struct arwill_config *config,
    struct arwill_event_log *log,
    struct arwill_service_manager *services
) {
    struct shell_environment environment;
    struct shell_session serial_session;
    struct shell_session remote_session;
    struct arwill_console remote_console;
    struct shell_system_task_context system_task_context;

    environment.filesystem = filesystem;
    environment.memory = memory;
    environment.power = power;
    environment.processes = processes;
    environment.pci = pci;
    environment.network = network;
    environment.ipv4 = ipv4;
    environment.remote_stream = arwill_ipv4_remote_stream(ipv4);
    environment.block_device = block_device;
    environment.interrupts = interrupts;
    environment.clock = clock;
    environment.user_runtime = user_runtime;
    environment.devices = devices;
    environment.config = config;
    environment.log = log;
    environment.services = services;

    remote_console.context = environment.remote_stream;
    remote_console.write = remote_console_write;
    remote_session.console = &remote_console;
    remote_session.active = 0;

    system_task_context.ipv4 = ipv4;
    system_task_context.remote_session = &remote_session;
    system_task_context.environment = &environment;

    initialize_shell_session(&serial_session, console, user_runtime, 0);
    if (!start_shell_system_tasks(processes, &system_task_context)) {
        arwill_console_write_line(console, "system tasks: initialization failed");
    }
    write_prompt(console, serial_session.current_directory);

    for (;;) {
        uint8_t byte = 0;
        if (arwill_input_try_read_byte(input, &byte)) {
            (void)handle_shell_byte(&serial_session, &environment, byte);
        }
        (void)arwill_process_run_system(processes);
        arwill_user_poll(user_runtime);
        finish_foreground_program(&serial_session, user_runtime, log);
        finish_foreground_program(&remote_session, user_runtime, log);
        service_top(&serial_session, &environment);
        service_top(&remote_session, &environment);
    }
}
