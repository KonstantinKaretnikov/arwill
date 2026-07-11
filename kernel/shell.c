#include <stddef.h>
#include <stdint.h>

#include <arwill/identity.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/shell.h>

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
    { .name = "pwd", .completion = shell_completion_none },
    { .name = "cd", .completion = shell_completion_directory_path },
    { .name = "clear", .completion = shell_completion_none },
    { .name = "ls", .completion = shell_completion_path },
    { .name = "cat", .completion = shell_completion_path },
    { .name = "stat", .completion = shell_completion_path },
    { .name = "meminfo", .completion = shell_completion_none },
    { .name = "ps", .completion = shell_completion_none },
    { .name = "run", .completion = shell_completion_process },
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
    arwill_console_write_line(console, "  pwd        show current directory");
    arwill_console_write_line(console, "  cd [path]  change current directory");
    arwill_console_write_line(console, "  clear      clear the terminal screen");
    arwill_console_write_line(console, "  ls [path]  list the read-only boot catalog");
    arwill_console_write_line(console, "  cat [path] show text file contents");
    arwill_console_write_line(console, "  stat [path] show file or directory metadata");
    arwill_console_write_line(console, "  meminfo    show memory map and page allocator");
    arwill_console_write_line(console, "  ps         show kernel process table");
    arwill_console_write_line(console, "  run [name] launch a built-in kernel process");
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
    arwill_console_write_line(console, "size: unknown");
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
}

static uint32_t shell_hello_process(const struct arwill_process_runtime *runtime) {
    if (runtime == 0 || runtime->context == 0) {
        return 1;
    }

    const struct shell_process_context *context =
        (const struct shell_process_context *)runtime->context;

    if (context->console == 0) {
        return 1;
    }

    arwill_console_write(context->console, "process ");
    arwill_console_write(context->console, runtime->name);
    arwill_console_write(context->console, ": hello from pid ");
    write_uint64_decimal(context->console, (uint64_t)runtime->pid);
    arwill_console_write_line(context->console, "");

    return 0;
}

static uint32_t shell_counter_process(const struct arwill_process_runtime *runtime) {
    if (runtime == 0 || runtime->context == 0) {
        return 1;
    }

    const struct shell_process_context *context =
        (const struct shell_process_context *)runtime->context;

    if (context->console == 0) {
        return 1;
    }

    for (uint64_t step = 1; step <= 3U; step++) {
        arwill_console_write(context->console, "process ");
        arwill_console_write(context->console, runtime->name);
        arwill_console_write(context->console, ": pid ");
        write_uint64_decimal(context->console, (uint64_t)runtime->pid);
        arwill_console_write(context->console, " step ");
        write_uint64_decimal(context->console, step);
        arwill_console_write_line(context->console, "/3");
    }

    return 0;
}

static const struct shell_builtin_process shell_builtin_processes[] = {
    { .name = "hello", .entry = shell_hello_process },
    { .name = "counter", .entry = shell_counter_process },
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
    const struct arwill_memory *memory,
    const struct arwill_power *power,
    struct arwill_process_manager *processes,
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

    if (string_equals(line, "ps")) {
        print_process_table(console, processes);
        return;
    }

    if (string_equals(line, "run") || starts_with(line, "run ")) {
        run_process(console, processes, process_context, argument_after_command(line));
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
    const struct arwill_memory *memory,
    const struct arwill_power *power,
    struct arwill_process_manager *processes
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

    write_prompt(console, current_directory);

    for (;;) {
        const uint8_t byte = arwill_input_read_byte(input);

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
