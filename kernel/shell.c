#include <stddef.h>
#include <stdint.h>

#include <arwill/identity.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/shell.h>

enum {
    shell_line_capacity = 96,
    shell_path_capacity = 96,
    ascii_backspace = 0x08,
    ascii_delete = 0x7f,
    ascii_carriage_return = '\r',
    ascii_line_feed = '\n',
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

static size_t string_length(const char *text) {
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int is_printable_ascii(uint8_t byte) {
    return byte >= 0x20 && byte <= 0x7e;
}

static void write_byte_echo(const struct arwill_console *console, uint8_t byte) {
    const char text[2] = { (char)byte, '\0' };

    arwill_console_write(console, text);
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

static void print_help(const struct arwill_console *console) {
    arwill_console_write_line(console, "commands:");
    arwill_console_write_line(console, "  help       show commands");
    arwill_console_write_line(console, "  version    show kernel version");
    arwill_console_write_line(console, "  pwd        show current directory");
    arwill_console_write_line(console, "  cd [path]  change current directory");
    arwill_console_write_line(console, "  ls [path]  list the read-only boot catalog");
    arwill_console_write_line(console, "  dir [path] alias for ls");
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
    char resolved_path[shell_path_capacity];

    if (!resolve_path(current_directory, path, resolved_path, sizeof(resolved_path))) {
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
    char resolved_path[shell_path_capacity];
    const char *target_path = path;

    if (target_path[0] == '\0') {
        target_path = "/";
    }

    if (!resolve_path(current_directory, target_path, resolved_path, sizeof(resolved_path))) {
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

static void run_command(
    const struct arwill_console *console,
    const struct arwill_filesystem *filesystem,
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

    if (string_equals(line, "dir") || starts_with(line, "dir ")) {
        print_listing(console, filesystem, current_directory, argument_after_command(line));
        return;
    }

    arwill_console_write(console, "unknown command: ");
    arwill_console_write_line(console, line);
}

void arwill_shell_run(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem
) {
    char line[shell_line_capacity];
    char current_directory[shell_path_capacity] = "/";
    size_t length = 0;

    write_prompt(console, current_directory);

    for (;;) {
        const uint8_t byte = arwill_input_read_byte(input);

        if (byte == ascii_carriage_return || byte == ascii_line_feed) {
            line[length] = '\0';
            arwill_console_write_line(console, "");
            run_command(console, filesystem, current_directory, line);
            length = 0;
            write_prompt(console, current_directory);
            continue;
        }

        if (byte == ascii_backspace || byte == ascii_delete) {
            if (length > 0) {
                length--;
                arwill_console_write(console, "\b \b");
            }
            continue;
        }

        if (!is_printable_ascii(byte)) {
            continue;
        }

        if (length >= shell_line_capacity - 1U) {
            continue;
        }

        line[length] = (char)byte;
        length++;
        write_byte_echo(console, byte);
    }
}
