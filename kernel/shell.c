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
    ascii_tab = '\t',
    ascii_delete = 0x7f,
    ascii_carriage_return = '\r',
    ascii_line_feed = '\n',
};

struct shell_command {
    const char *name;
    int accepts_path;
};

static const struct shell_command shell_commands[] = {
    { .name = "help", .accepts_path = 0 },
    { .name = "version", .accepts_path = 0 },
    { .name = "pwd", .accepts_path = 0 },
    { .name = "cd", .accepts_path = 1 },
    { .name = "ls", .accepts_path = 1 },
    { .name = "dir", .accepts_path = 1 },
    { .name = "cat", .accepts_path = 1 },
    { .name = "halt", .accepts_path = 0 },
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
    arwill_console_write_line(console, "  ls [path]  list the read-only boot catalog");
    arwill_console_write_line(console, "  dir [path] alias for ls");
    arwill_console_write_line(console, "  cat [path] show text file contents");
    arwill_console_write_line(console, "  Tab        complete commands and paths");
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

static int command_accepts_path(const char *command) {
    const size_t command_count = sizeof(shell_commands) / sizeof(shell_commands[0]);

    for (size_t index = 0; index < command_count; index++) {
        if (string_equals(command, shell_commands[index].name)) {
            return shell_commands[index].accepts_path;
        }
    }

    return 0;
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

        if (command_accepts_path(single_match)) {
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

    if (!command_accepts_path(command)) {
        return;
    }

    complete_path(
        console,
        filesystem,
        current_directory,
        line,
        length,
        argument_start,
        string_equals(command, "cd")
    );
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

    if (string_equals(line, "cat") || starts_with(line, "cat ")) {
        print_file(console, filesystem, current_directory, argument_after_command(line));
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

        if (byte == ascii_tab) {
            complete_line(console, filesystem, current_directory, line, &length);
            continue;
        }

        if (!is_printable_ascii(byte)) {
            continue;
        }

        (void)append_char_to_line(console, line, &length, (char)byte);
    }
}
