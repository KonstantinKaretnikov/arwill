#include <stddef.h>
#include <stdint.h>

#include <arwill/identity.h>
#include <arwill/kernel/block_device.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/crypto.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/device.h>
#include <arwill/kernel/entropy.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/scheduler.h>
#include <arwill/kernel/shell.h>
#include <arwill/kernel/tcp.h>
#include <arwill/kernel/user.h>

enum {
    shell_line_capacity = 96,
    shell_path_capacity = 96,
    shell_history_capacity = 8,
    ascii_backspace = 0x08,
    ascii_escape = 0x1b,
    ascii_tab = '\t',
    ascii_delete = 0x7f,
    ascii_carriage_return = '\r',
    ascii_line_feed = '\n',
    ascii_left_bracket = '[',
    ascii_arrow_up = 'A',
    ascii_arrow_down = 'B',
    utf8_cyrillic_lead_0 = 0xd0,
    utf8_cyrillic_lead_1 = 0xd1,
};

enum shell_completion_kind {
    shell_completion_none,
    shell_completion_path,
    shell_completion_directory_path,
    shell_completion_process
};

struct shell_command {
    const char *name;
    enum shell_completion_kind completion;
};

static const struct shell_command shell_commands[] = {
    { .name = "help", .completion = shell_completion_none },
    { .name = "version", .completion = shell_completion_none },
    { .name = "uptime", .completion = shell_completion_none },
    { .name = "pciinfo", .completion = shell_completion_none },
    { .name = "netinfo", .completion = shell_completion_none },
    { .name = "netprobe", .completion = shell_completion_none },
    { .name = "netcfg", .completion = shell_completion_none },
    { .name = "arping", .completion = shell_completion_none },
    { .name = "ping", .completion = shell_completion_none },
    { .name = "tcpcheck", .completion = shell_completion_none },
    { .name = "tcplisten", .completion = shell_completion_none },
    { .name = "tcpinfo", .completion = shell_completion_none },
    { .name = "cryptocheck", .completion = shell_completion_none },
    { .name = "entropyinfo", .completion = shell_completion_none },
    { .name = "pwd", .completion = shell_completion_none },
    { .name = "cd", .completion = shell_completion_directory_path },
    { .name = "clear", .completion = shell_completion_none },
    { .name = "ls", .completion = shell_completion_path },
    { .name = "cat", .completion = shell_completion_path },
    { .name = "mkdir", .completion = shell_completion_directory_path },
    { .name = "write", .completion = shell_completion_path },
    { .name = "writehex", .completion = shell_completion_path },
    { .name = "rm", .completion = shell_completion_path },
    { .name = "stat", .completion = shell_completion_path },
    { .name = "meminfo", .completion = shell_completion_none },
    { .name = "heaptest", .completion = shell_completion_none },
    { .name = "devices", .completion = shell_completion_none },
    { .name = "blkinfo", .completion = shell_completion_none },
    { .name = "irqinfo", .completion = shell_completion_none },
    { .name = "irqprobe", .completion = shell_completion_none },
    { .name = "schedinfo", .completion = shell_completion_none },
    { .name = "userinfo", .completion = shell_completion_none },
    { .name = "ownerinfo", .completion = shell_completion_none },
    { .name = "ps", .completion = shell_completion_none },
    { .name = "run", .completion = shell_completion_process },
    { .name = "exec", .completion = shell_completion_path },
    { .name = "step", .completion = shell_completion_none },
    { .name = "exit", .completion = shell_completion_none },
    { .name = "halt", .completion = shell_completion_none },
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

struct shell_builtin_process {
    const char *name;
    arwill_process_entry entry;
};

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

static int starts_with(const char *text, const char *prefix) {
    size_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }

        index++;
    }

    return 1;
}

static int starts_with_sized(const char *text, const char *prefix, size_t prefix_length) {
    for (size_t index = 0; index < prefix_length; index++) {
        if (text[index] != prefix[index]) {
            return 0;
        }
    }

    return 1;
}

static size_t string_length(const char *text) {
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
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
    const char *replacement
) {
    erase_line_contents(console, *length);

    *length = 0;
    line[0] = '\0';

    while (replacement[*length] != '\0' && *length < shell_line_capacity - 1U) {
        line[*length] = replacement[*length];
        write_byte_echo(console, (uint8_t)replacement[*length]);
        *length = *length + 1U;
    }

    line[*length] = '\0';
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

    if (history->count > 0U && string_equals(history->entries[history->count - 1U], line)) {
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
    replace_line(console, line, length, history->entries[*history_position]);
}

static void history_next(
    const struct arwill_console *console,
    const struct shell_history *history,
    char *line,
    size_t *length,
    size_t *history_position
) {
    if (history->count == 0U || *history_position >= history->count) {
        return;
    }

    *history_position = *history_position + 1U;

    if (*history_position == history->count) {
        replace_line(console, line, length, "");
        return;
    }

    replace_line(console, line, length, history->entries[*history_position]);
}

static void path_set_root(char *path, size_t capacity) {
    if (capacity >= 2U) {
        path[0] = '/';
        path[1] = '\0';
    }
}

static void path_pop_segment(char *path) {
    size_t length = string_length(path);

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

    size_t path_length = string_length(path);

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

static void print_help(const struct arwill_console *console) {
    arwill_console_write_line(console, "commands:");
    arwill_console_write_line(console, "  help       show commands");
    arwill_console_write_line(console, "  version    show kernel version");
    arwill_console_write_line(console, "  uptime     show monotonic time since boot");
    arwill_console_write_line(console, "  pciinfo    list discovered PCI devices");
    arwill_console_write_line(console, "  netinfo    show network device diagnostics");
    arwill_console_write_line(console, "  netprobe   transmit a raw Ethernet diagnostic frame");
    arwill_console_write_line(console, "  netcfg     show fixed IPv4 network configuration");
    arwill_console_write_line(console, "  arping     transmit an ARP request to the gateway");
    arwill_console_write_line(console, "  ping       send one ICMP echo to the gateway");
    arwill_console_write_line(console, "  tcpcheck   exercise the TCP listener handshake");
    arwill_console_write_line(console, "  tcplisten  poll for TCP port 22 connections");
    arwill_console_write_line(console, "  tcpinfo    show TCP port 22 listener state");
    arwill_console_write_line(console, "  cryptocheck verify the SHA-256 primitive");
    arwill_console_write_line(console, "  entropyinfo show hardware entropy status");
    arwill_console_write_line(console, "  pwd        show current directory");
    arwill_console_write_line(console, "  cd [path]  change current directory");
    arwill_console_write_line(console, "  clear      clear the terminal screen");
    arwill_console_write_line(console, "  ls [path]  list the current filesystem");
    arwill_console_write_line(console, "  cat [path] show text file contents");
    arwill_console_write_line(console, "  mkdir [path] create a directory");
    arwill_console_write_line(console, "  write [path] [text] create or overwrite a text file");
    arwill_console_write_line(console, "  writehex [path] [hex] create or overwrite a binary file");
    arwill_console_write_line(console, "  rm [path] remove a file or empty directory");
    arwill_console_write_line(console, "  stat [path] show file or directory metadata");
    arwill_console_write_line(console, "  meminfo    show memory map and allocators");
    arwill_console_write_line(console, "  heaptest   exercise kernel heap allocation");
    arwill_console_write_line(console, "  devices    list detected devices");
    arwill_console_write_line(console, "  blkinfo    show block device read diagnostics");
    arwill_console_write_line(console, "  irqinfo    show interrupt and timer diagnostics");
    arwill_console_write_line(console, "  irqprobe   trigger a safe breakpoint exception");
    arwill_console_write_line(console, "  schedinfo  show scheduler tick diagnostics");
    arwill_console_write_line(console, "  userinfo   show user-mode diagnostics");
    arwill_console_write_line(console, "  ownerinfo  show the OS ownership model");
    arwill_console_write_line(console, "  ps         show kernel process table");
    arwill_console_write_line(console, "  run [name] launch a built-in kernel process");
    arwill_console_write_line(console, "  exec [path] run a stored program image");
    arwill_console_write_line(console, "  step       run one cooperative process step");
    arwill_console_write_line(console, "  exit       power off the machine");
    arwill_console_write_line(console, "  Tab        complete commands, paths, and processes");
    arwill_console_write_line(console, "  Up/Down    browse command history");
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

    const size_t contents_length = string_length(file.contents);
    if (contents_length == 0U || file.contents[contents_length - 1U] != '\n') {
        arwill_console_write_line(console, "");
    }
}

static void exec_program_image(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
    const struct arwill_user_runtime *user_runtime,
    const char *current_directory,
    const char *path
) {
    char path_argument[shell_path_capacity];
    char resolved_path[shell_path_capacity];

    if (!copy_first_argument(path_argument, sizeof(path_argument), path)) {
        arwill_console_write_line(console, "exec: path too long");
        return;
    }

    if (path_argument[0] == '\0') {
        arwill_console_write_line(console, "exec: missing path");
        return;
    }

    if (!resolve_path(current_directory, path_argument, resolved_path, sizeof(resolved_path))) {
        arwill_console_write_line(console, "exec: path too long");
        return;
    }

    struct arwill_fs_file file;

    if (!arwill_filesystem_read_file(filesystem, resolved_path, &file)) {
        arwill_console_write(console, "exec: no such file: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    if (file.type != arwill_fs_file_binary || file.contents == 0) {
        arwill_console_write(console, "exec: not a program image: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    struct arwill_user_program_result result;

    if (!arwill_user_run_image(
            user_runtime,
            (const uint8_t *)file.contents,
            file.size_bytes,
            console,
            &result
        )) {
        arwill_console_write(console, "exec: launch failed: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    if (!result.exited) {
        arwill_console_write_line(console, "exec: program did not exit");
        return;
    }

    arwill_console_write(console, "exec: exited ");
    write_uint64_decimal(console, (uint64_t)result.exit_code);
    arwill_console_write_line(console, "");
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
            string_length(contents))) {
        arwill_console_write(console, "write: cannot write: ");
        arwill_console_write_line(console, resolved_path);
        return;
    }

    arwill_console_write(console, "write: wrote ");
    write_uint64_decimal(console, (uint64_t)string_length(contents));
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
    const size_t hex_length = string_length(hex);

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
}

static void print_owner_info(const struct arwill_console *console) {
    arwill_console_write_line(console, "owner model: " ARWILL_OWNER_MODEL);
    arwill_console_write_line(console, "accounts: none");
    arwill_console_write_line(console, "owner access: full system control");
    arwill_console_write_line(console, "kernel boundary: ring 3 programs use syscalls");
    arwill_console_write_line(console, "privileged code: explicit kernel or driver work");
}

static struct arwill_process_result shell_hello_process(
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

    arwill_console_write(context->console, "process ");
    arwill_console_write(context->console, runtime->name);
    arwill_console_write(context->console, ": hello from pid ");
    write_uint64_decimal(context->console, (uint64_t)runtime->pid);
    arwill_console_write_line(context->console, "");

    return arwill_process_finish(0);
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

    const uint64_t step = runtime->run_count + 1U;

    arwill_console_write(context->console, "process ");
    arwill_console_write(context->console, runtime->name);
    arwill_console_write(context->console, ": pid ");
    write_uint64_decimal(context->console, (uint64_t)runtime->pid);
    arwill_console_write(context->console, " step ");
    write_uint64_decimal(context->console, step);
    arwill_console_write_line(context->console, "/3");

    if (step < 3U) {
        return arwill_process_yield();
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
    { .name = "hello", .entry = shell_hello_process },
    { .name = "counter", .entry = shell_counter_process },
    { .name = "userhello", .entry = shell_user_hello_process },
    { .name = "userbad", .entry = shell_user_bad_process },
};

static const struct shell_builtin_process *find_builtin_process(const char *name) {
    const size_t process_count =
        sizeof(shell_builtin_processes) / sizeof(shell_builtin_processes[0]);

    for (size_t index = 0; index < process_count; index++) {
        if (string_equals(name, shell_builtin_processes[index].name)) {
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
    const struct arwill_process_manager *processes
) {
    const struct arwill_process *table = arwill_process_table(processes);
    int saw_process = 0;

    if (table == 0) {
        arwill_console_write_line(console, "ps: process manager unavailable");
        return;
    }

    arwill_console_write_line(console, "pid state runs exit name");

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        const struct arwill_process *process = &table[index];

        if (process->state == arwill_process_state_empty) {
            continue;
        }

        saw_process = 1;
        write_uint64_decimal(console, (uint64_t)process->pid);
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
        if (string_equals(command, shell_commands[index].name)) {
            return shell_commands[index].completion;
        }
    }

    return shell_completion_none;
}

static void show_command_candidates(
    const struct arwill_console *console,
    const char *current_directory,
    const char *line,
    const char *prefix,
    size_t prefix_length
) {
    const size_t command_count = sizeof(shell_commands) / sizeof(shell_commands[0]);

    arwill_console_write_line(console, "");

    for (size_t index = 0; index < command_count; index++) {
        if (starts_with_sized(shell_commands[index].name, prefix, prefix_length)) {
            arwill_console_write_line(console, shell_commands[index].name);
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
    const size_t command_count = sizeof(shell_commands) / sizeof(shell_commands[0]);
    const char *single_match = 0;
    size_t match_count = 0;
    size_t shared_length = 0;

    for (size_t index = 0; index < command_count; index++) {
        const char *candidate = shell_commands[index].name;

        if (!starts_with_sized(candidate, line, *length)) {
            continue;
        }

        if (match_count == 0U) {
            single_match = candidate;
            shared_length = string_length(candidate);
        } else {
            shared_length = common_prefix_length(single_match, candidate, shared_length);
        }

        match_count++;
    }

    if (match_count == 0U) {
        return;
    }

    if (match_count == 1U && single_match != 0) {
        const size_t candidate_length = string_length(single_match);

        for (size_t index = *length; index < candidate_length; index++) {
            if (!append_char_to_line(console, line, length, single_match[index])) {
                return;
            }
        }

        if (command_completion(single_match) != shell_completion_none) {
            (void)append_char_to_line(console, line, length, ' ');
        }

        return;
    }

    if (shared_length > *length) {
        for (size_t index = *length; index < shared_length; index++) {
            if (!append_char_to_line(console, line, length, single_match[index])) {
                return;
            }
        }

        return;
    }

    line[*length] = '\0';
    show_command_candidates(console, current_directory, line, line, *length);
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

static void split_path_for_completion(
    const char *path,
    char *parent_path,
    size_t parent_capacity,
    char *prefix,
    size_t prefix_capacity
) {
    size_t last_slash = 0;
    size_t length = string_length(path);
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

        if (!starts_with_sized(entry->name, prefix, string_length(prefix))) {
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

    const size_t prefix_length = string_length(prefix);

    for (size_t index = 0; index < listing.count; index++) {
        const struct arwill_fs_entry *entry = &listing.entries[index];

        if (directories_only && entry->type != arwill_fs_entry_directory) {
            continue;
        }

        if (!starts_with_sized(entry->name, prefix, prefix_length)) {
            continue;
        }

        if (match_count == 0U) {
            single_match = entry;
            shared_length = string_length(entry->name);
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
        const size_t match_length = string_length(single_match->name);

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

static void show_process_candidates(
    const struct arwill_console *console,
    const char *current_directory,
    const char *line,
    const char *prefix
) {
    const size_t process_count =
        sizeof(shell_builtin_processes) / sizeof(shell_builtin_processes[0]);
    const size_t prefix_length = string_length(prefix);

    arwill_console_write_line(console, "");

    for (size_t index = 0; index < process_count; index++) {
        if (starts_with_sized(shell_builtin_processes[index].name, prefix, prefix_length)) {
            arwill_console_write_line(console, shell_builtin_processes[index].name);
        }
    }

    redraw_line(console, current_directory, line);
}

static void complete_process_name(
    const struct arwill_console *console,
    const char *current_directory,
    char *line,
    size_t *length,
    size_t argument_start
) {
    char process_prefix[shell_line_capacity];
    const char *single_match = 0;
    size_t match_count = 0;
    size_t shared_length = 0;
    const size_t process_count =
        sizeof(shell_builtin_processes) / sizeof(shell_builtin_processes[0]);

    if (!copy_string(process_prefix, sizeof(process_prefix), &line[argument_start])) {
        return;
    }

    const size_t prefix_length = string_length(process_prefix);

    for (size_t index = 0; index < process_count; index++) {
        const char *candidate = shell_builtin_processes[index].name;

        if (!starts_with_sized(candidate, process_prefix, prefix_length)) {
            continue;
        }

        if (match_count == 0U) {
            single_match = candidate;
            shared_length = string_length(candidate);
        } else {
            shared_length = common_prefix_length(single_match, candidate, shared_length);
        }

        match_count++;
    }

    if (match_count == 0U) {
        return;
    }

    if (match_count == 1U && single_match != 0) {
        const size_t candidate_length = string_length(single_match);

        for (size_t index = prefix_length; index < candidate_length; index++) {
            if (!append_char_to_line(console, line, length, single_match[index])) {
                return;
            }
        }

        (void)append_char_to_line(console, line, length, ' ');
        return;
    }

    if (shared_length > prefix_length && single_match != 0) {
        for (size_t index = prefix_length; index < shared_length; index++) {
            if (!append_char_to_line(console, line, length, single_match[index])) {
                return;
            }
        }

        return;
    }

    line[*length] = '\0';
    show_process_candidates(console, current_directory, line, process_prefix);
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

    if (line[argument_start] == '\0' && argument_start >= *length) {
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
    const struct arwill_console *console,
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
    struct shell_process_context *process_context,
    char *current_directory,
    const char *line
) {
    if (string_equals(line, "")) {
        return;
    }

    if (string_equals(line, "help")) {
        print_help(console);
        return;
    }

    if (string_equals(line, "version")) {
        print_version(console);
        return;
    }

    if (string_equals(line, "uptime")) {
        print_uptime(console, clock);
        return;
    }

    if (string_equals(line, "pciinfo")) {
        arwill_console_write_line(console, "pci: x86_64 configuration mechanism 1");
        arwill_console_write(console, "devices: ");
        write_size_decimal(console, pci == 0 ? 0U : pci->count);
        arwill_console_write_line(console, "");
        if (pci != 0) {
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
        return;
    }

    if (string_equals(line, "netinfo")) {
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
        return;
    }

    if (string_equals(line, "netprobe")) {
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

    if (string_equals(line, "netcfg")) {
        arwill_ipv4_print_config(ipv4, console);
        return;
    }

    if (string_equals(line, "arping")) {
        if (ipv4 == 0 || !arwill_ipv4_send_arp_request(ipv4, ipv4->gateway)) {
            arwill_console_write_line(console, "arping: transmit failed");
            return;
        }
        arwill_console_write_line(console, "arping: request transmitted to 10.0.2.2");
        return;
    }

    if (string_equals(line, "ping")) {
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
        return;
    }

    if (string_equals(line, "tcpcheck")) {
        struct arwill_tcp_listener listener;
        struct arwill_tcp_segment syn;
        struct arwill_tcp_segment reply;
        struct arwill_tcp_segment ack;

        arwill_tcp_listener_init(&listener, 22U, 0x41520000U);
        syn.source_port = 4242U;
        syn.destination_port = 22U;
        syn.sequence = 100U;
        syn.acknowledgement = 0U;
        syn.flags = arwill_tcp_flag_syn;
        if (!arwill_tcp_listener_receive(&listener, &syn, &reply) ||
            reply.flags != (arwill_tcp_flag_syn | arwill_tcp_flag_ack) ||
            reply.acknowledgement != 101U) {
            arwill_console_write_line(console, "tcpcheck: SYN/SYN-ACK failed");
            return;
        }
        ack.source_port = syn.source_port;
        ack.destination_port = 22U;
        ack.sequence = 101U;
        ack.acknowledgement = reply.sequence + 1U;
        ack.flags = arwill_tcp_flag_ack;
        if (!arwill_tcp_listener_receive(&listener, &ack, &reply)) {
            arwill_console_write_line(console, "tcpcheck: ACK failed");
            return;
        }
        arwill_console_write(console, "tcpcheck: listener state ");
        arwill_console_write_line(console, arwill_tcp_state_name(listener.state));
        return;
    }

    if (string_equals(line, "tcplisten")) {
        size_t processed = 0;
        if (ipv4 == 0 || !arwill_ipv4_service_tcp(ipv4, &processed)) {
            arwill_console_write_line(console, "tcplisten: network unavailable");
            return;
        }
        arwill_console_write(console, "tcplisten: frames ");
        write_size_decimal(console, processed);
        arwill_console_write(console, ", state ");
        arwill_console_write_line(console, arwill_tcp_state_name(ipv4->tcp_listener.state));
        arwill_console_write(console, "tcp frames: ");
        write_uint64_decimal(console, ipv4->tcp_frames_received);
        arwill_console_write(console, ", syn-ack: ");
        write_uint64_decimal(console, ipv4->tcp_syn_ack_sent);
        arwill_console_write(console, ", ssh banners: ");
        write_uint64_decimal(console, ipv4->ssh_banners_sent);
        arwill_console_write_line(console, "");
        arwill_console_write(console, "ssh client: ");
        arwill_console_write_line(console, ipv4->ssh_client_identification_received ?
            ipv4->ssh_client_identification : "not received");
        return;
    }

    if (string_equals(line, "tcpinfo")) {
        if (ipv4 == 0) {
            arwill_console_write_line(console, "tcp: unavailable");
            return;
        }
        arwill_console_write(console, "tcp: port ");
        write_uint64_decimal(console, ipv4->tcp_listener.port);
        arwill_console_write(console, ", state ");
        arwill_console_write_line(console, arwill_tcp_state_name(ipv4->tcp_listener.state));
        arwill_console_write(console, "tcp frames: ");
        write_uint64_decimal(console, ipv4->tcp_frames_received);
        arwill_console_write(console, ", syn-ack: ");
        write_uint64_decimal(console, ipv4->tcp_syn_ack_sent);
        arwill_console_write(console, ", ssh banners: ");
        write_uint64_decimal(console, ipv4->ssh_banners_sent);
        arwill_console_write_line(console, "");
        return;
    }

    if (string_equals(line, "cryptocheck")) {
        static const uint8_t expected_sha256[arwill_sha256_size] = {
            0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
            0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U,
            0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U, 0x7aU, 0x9cU,
            0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU,
        };
        static const uint8_t x25519_scalar[arwill_x25519_size] = {
            0xa5U, 0x46U, 0xe3U, 0x6bU, 0xf0U, 0x52U, 0x7cU, 0x9dU,
            0x3bU, 0x16U, 0x15U, 0x4bU, 0x82U, 0x46U, 0x5eU, 0xddU,
            0x62U, 0x14U, 0x4cU, 0x0aU, 0xc1U, 0xfcU, 0x5aU, 0x18U,
            0x50U, 0x6aU, 0x22U, 0x44U, 0xbaU, 0x44U, 0x9aU, 0xc4U,
        };
        static const uint8_t x25519_point[arwill_x25519_size] = {
            0xe6U, 0xdbU, 0x68U, 0x67U, 0x58U, 0x30U, 0x30U, 0xdbU,
            0x35U, 0x94U, 0xc1U, 0xa4U, 0x24U, 0xb1U, 0x5fU, 0x7cU,
            0x72U, 0x66U, 0x24U, 0xecU, 0x26U, 0xb3U, 0x35U, 0x3bU,
            0x10U, 0xa9U, 0x03U, 0xa6U, 0xd0U, 0xabU, 0x1cU, 0x4cU,
        };
        static const uint8_t expected_x25519[arwill_x25519_size] = {
            0xc3U, 0xdaU, 0x55U, 0x37U, 0x9dU, 0xe9U, 0xc6U, 0x90U,
            0x8eU, 0x94U, 0xeaU, 0x4dU, 0xf2U, 0x8dU, 0x08U, 0x4fU,
            0x32U, 0xecU, 0xcfU, 0x03U, 0x49U, 0x1cU, 0x71U, 0xf7U,
            0x54U, 0xb4U, 0x07U, 0x55U, 0x77U, 0xa2U, 0x85U, 0x52U,
        };
        uint8_t digest[arwill_sha256_size];
        uint8_t x25519_output[arwill_x25519_size];
        int sha256_matches = 1;
        int x25519_matches;

        arwill_crypto_sha256("abc", 3U, digest);
        for (size_t index = 0; index < arwill_sha256_size; index++) {
            if (digest[index] != expected_sha256[index]) {
                sha256_matches = 0;
            }
        }

        arwill_console_write_line(console, sha256_matches ?
            "cryptocheck: sha256 abc passed" : "cryptocheck: sha256 abc failed");

        x25519_matches = arwill_crypto_x25519(
            x25519_output,
            x25519_scalar,
            x25519_point
        );
        for (size_t index = 0; index < arwill_x25519_size; index++) {
            if (x25519_output[index] != expected_x25519[index]) {
                x25519_matches = 0;
            }
        }
        arwill_console_write_line(console, x25519_matches ?
            "cryptocheck: x25519 rfc7748 passed" :
            "cryptocheck: x25519 rfc7748 failed");
        return;
    }

    if (string_equals(line, "entropyinfo")) {
        uint8_t sample[32];
        const int available = arwill_entropy_available();

        arwill_console_write(console, "entropy: ");
        arwill_console_write_line(console, arwill_entropy_source_name());
        arwill_console_write(console, "available: ");
        arwill_console_write_line(console, available ? "yes" : "no");
        if (!available) {
            arwill_console_write_line(console, "sample: unavailable");
            return;
        }

        arwill_console_write_line(console, arwill_entropy_fill(sample, sizeof(sample)) ?
            "sample: acquired 32 bytes" : "sample: acquisition failed");
        return;
    }

    if (string_equals(line, "pwd")) {
        arwill_console_write_line(console, current_directory);
        return;
    }

    if (string_equals(line, "clear")) {
        clear_screen(console);
        return;
    }

    if (string_equals(line, "meminfo")) {
        print_meminfo(console, memory);
        return;
    }

    if (string_equals(line, "heaptest")) {
        run_heap_test(console, memory);
        return;
    }

    if (string_equals(line, "devices")) {
        print_devices(console, devices);
        return;
    }

    if (string_equals(line, "blkinfo")) {
        print_block_info(console, block_device);
        return;
    }

    if (string_equals(line, "irqinfo")) {
        print_irqinfo(console, interrupts);
        return;
    }

    if (string_equals(line, "irqprobe")) {
        probe_interrupts(console, interrupts);
        return;
    }

    if (string_equals(line, "schedinfo")) {
        print_scheduler_info(console, interrupts);
        return;
    }

    if (string_equals(line, "userinfo")) {
        print_user_info(console, user_runtime);
        return;
    }

    if (string_equals(line, "ownerinfo")) {
        print_owner_info(console);
        return;
    }

    if (string_equals(line, "ps")) {
        print_process_table(console, processes);
        return;
    }

    if (string_equals(line, "run") || starts_with(line, "run ")) {
        run_process(console, processes, process_context, argument_after_command(line));
        return;
    }

    if (string_equals(line, "exec") || starts_with(line, "exec ")) {
        exec_program_image(
            console,
            filesystem,
            user_runtime,
            current_directory,
            argument_after_command(line)
        );
        return;
    }

    if (string_equals(line, "step")) {
        step_processes(console, processes);
        return;
    }

    if (string_equals(line, "exit")) {
        arwill_console_write_line(console, "status: powering off");
        arwill_poweroff(power);
    }

    if (string_equals(line, "cd") || starts_with(line, "cd ")) {
        change_directory(console, filesystem, current_directory, argument_after_command(line));
        return;
    }

    if (string_equals(line, "halt")) {
        arwill_console_write_line(console, "status: shell halted");
        arwill_cpu_idle_forever();
    }

    if (string_equals(line, "ls") || starts_with(line, "ls ")) {
        print_listing(console, filesystem, current_directory, argument_after_command(line));
        return;
    }

    if (string_equals(line, "cat") || starts_with(line, "cat ")) {
        print_file(console, filesystem, current_directory, argument_after_command(line));
        return;
    }

    if (string_equals(line, "mkdir") || starts_with(line, "mkdir ")) {
        mutate_path(console, filesystem, current_directory,
            argument_after_command(line), "mkdir", 1);
        return;
    }

    if (string_equals(line, "write") || starts_with(line, "write ")) {
        write_file(console, filesystem, current_directory, argument_after_command(line));
        return;
    }

    if (string_equals(line, "writehex") || starts_with(line, "writehex ")) {
        write_hex_file(console, filesystem, current_directory, argument_after_command(line));
        return;
    }

    if (string_equals(line, "rm") || starts_with(line, "rm ")) {
        mutate_path(console, filesystem, current_directory,
            argument_after_command(line), "rm", 0);
        return;
    }

    if (string_equals(line, "stat") || starts_with(line, "stat ")) {
        print_stat(
            console,
            filesystem,
            current_directory,
            argument_after_command(line),
            "stat"
        );
        return;
    }

    arwill_console_write(console, "unknown command: ");
    arwill_console_write_line(console, line);
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
    const struct arwill_device_registry *devices
) {
    char line[shell_line_capacity];
    char current_directory[shell_path_capacity] = "/";
    size_t length = 0;
    struct shell_history history;
    size_t history_position = 0;
    enum shell_escape_state escape_state = shell_escape_none;
    struct shell_input_normalizer normalizer;
    struct shell_process_context process_context;

    history.count = 0;
    normalizer.utf8_state = shell_utf8_none;
    normalizer.utf8_lead = 0;
    normalizer.russian_layout_active = 0;
    process_context.console = console;
    process_context.user_runtime = user_runtime;

    write_prompt(console, current_directory);

    for (;;) {
        uint8_t byte = 0;
        while (!arwill_input_try_read_byte(input, &byte)) {
            if (ipv4 != 0) {
                (void)arwill_ipv4_poll_tcp(ipv4);
            }
        }

        if (escape_state == shell_escape_started) {
            if (byte == ascii_left_bracket) {
                escape_state = shell_escape_bracket;
            } else {
                escape_state = shell_escape_none;
            }
            continue;
        }

        if (escape_state == shell_escape_bracket) {
            if (byte == ascii_arrow_up) {
                history_previous(console, &history, line, &length, &history_position);
            } else if (byte == ascii_arrow_down) {
                history_next(console, &history, line, &length, &history_position);
            }

            escape_state = shell_escape_none;
            continue;
        }

        if (byte == ascii_escape) {
            escape_state = shell_escape_started;
            continue;
        }

        if (byte == ascii_carriage_return || byte == ascii_line_feed) {
            line[length] = '\0';
            arwill_console_write_line(console, "");
            history_add(&history, line);
            history_position = history.count;
            run_command(
                console,
                filesystem,
                memory,
                power,
                processes,
                pci,
                network,
                ipv4,
                block_device,
                interrupts,
                clock,
                user_runtime,
                devices,
                &process_context,
                current_directory,
                line
            );
            length = 0;
            normalizer.utf8_state = shell_utf8_none;
            normalizer.russian_layout_active = 0;
            write_prompt(console, current_directory);
            continue;
        }

        if (byte == ascii_backspace || byte == ascii_delete) {
            if (length > 0) {
                length--;
                arwill_console_write(console, "\b \b");
                if (length == 0U) {
                    normalizer.russian_layout_active = 0;
                }
            }
            continue;
        }

        if (byte == ascii_tab) {
            complete_line(console, filesystem, current_directory, line, &length);
            continue;
        }

        char input_char;

        if (!normalize_text_input_byte(&normalizer, byte, &input_char)) {
            continue;
        }

        history_position = history.count;
        (void)append_char_to_line(console, line, &length, input_char);
    }
}
