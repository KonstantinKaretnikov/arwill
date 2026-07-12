#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/arfs.h>
#include <arwill/kernel/block_device.h>
#include <arwill/kernel/filesystem.h>

enum {
    arfs_sector_size = 512,
    arfs_superblock_lba = 3,
    arfs_max_manifest_sectors = 2,
    arfs_max_entries = 16,
    arfs_max_listing_entries = 16,
    arfs_max_path_length = 64,
    arfs_max_name_length = 32,
    arfs_file_buffer_capacity = 2048,
    arfs_write_buffer_capacity = 512
};

enum arfs_entry_kind {
    arfs_entry_directory,
    arfs_entry_file
};

struct arfs_entry {
    enum arfs_entry_kind kind;
    enum arwill_fs_file_type file_type;
    char path[arfs_max_path_length];
    char name[arfs_max_name_length];
    uint64_t data_lba;
    uint64_t size_bytes;
};

struct arfs_context {
    const struct arwill_block_device *block_device;
    struct arfs_entry entries[arfs_max_entries];
    size_t entry_count;
    struct arwill_fs_entry listing_entries[arfs_max_listing_entries];
    uint8_t manifest[arfs_max_manifest_sectors * arfs_sector_size];
    char file_buffer[arfs_file_buffer_capacity];
    uint8_t write_buffer[arfs_write_buffer_capacity];
    uint64_t writable_state_lba;
    uint64_t owner_note_lba;
    uint64_t owner_note_capacity;
};

static struct arfs_context arfs;

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

static int copy_token(
    char *destination,
    size_t capacity,
    const char *source,
    size_t length
) {
    if (capacity == 0U || length >= capacity) {
        return 0;
    }

    for (size_t index = 0; index < length; index++) {
        destination[index] = source[index];
    }

    destination[length] = '\0';
    return 1;
}

static int parse_decimal_token(const char *text, size_t length, uint64_t *value) {
    uint64_t result = 0;

    if (length == 0U || value == 0) {
        return 0;
    }

    for (size_t index = 0; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }

        const uint64_t digit = (uint64_t)(text[index] - '0');

        if (result > (UINT64_MAX - digit) / 10U) {
            return 0;
        }

        result = (result * 10U) + digit;
    }

    *value = result;
    return 1;
}

static const char *next_token(
    const char *cursor,
    const char *line_end,
    const char **token_start,
    size_t *token_length
) {
    while (cursor < line_end && *cursor == ' ') {
        cursor++;
    }

    *token_start = cursor;

    while (cursor < line_end && *cursor != ' ') {
        cursor++;
    }

    *token_length = (size_t)(cursor - *token_start);
    return cursor;
}

static void derive_name(const char *path, char *name, size_t capacity) {
    size_t last_slash = 0;
    const size_t length = string_length(path);

    for (size_t index = 0; index < length; index++) {
        if (path[index] == '/') {
            last_slash = index;
        }
    }

    if (last_slash + 1U >= length) {
        (void)copy_token(name, capacity, "/", 1);
        return;
    }

    (void)copy_token(name, capacity, &path[last_slash + 1U], length - last_slash - 1U);
}

static int add_directory_entry(const char *path, size_t path_length) {
    if (arfs.entry_count >= arfs_max_entries) {
        return 0;
    }

    struct arfs_entry *entry = &arfs.entries[arfs.entry_count];

    entry->kind = arfs_entry_directory;
    entry->file_type = arwill_fs_file_binary;
    entry->data_lba = 0;
    entry->size_bytes = 0;

    if (!copy_token(entry->path, sizeof(entry->path), path, path_length)) {
        return 0;
    }

    derive_name(entry->path, entry->name, sizeof(entry->name));
    arfs.entry_count++;
    return 1;
}

static int add_file_entry(
    const char *path,
    size_t path_length,
    const char *type,
    size_t type_length,
    uint64_t data_lba,
    uint64_t size_bytes
) {
    if (arfs.entry_count >= arfs_max_entries) {
        return 0;
    }

    struct arfs_entry *entry = &arfs.entries[arfs.entry_count];

    entry->kind = arfs_entry_file;
    entry->data_lba = data_lba;
    entry->size_bytes = size_bytes;

    if (type_length == 4U && starts_with(type, "text")) {
        entry->file_type = arwill_fs_file_text;
    } else if (type_length == 6U && starts_with(type, "binary")) {
        entry->file_type = arwill_fs_file_binary;
    } else {
        return 0;
    }

    if (!copy_token(entry->path, sizeof(entry->path), path, path_length)) {
        return 0;
    }

    derive_name(entry->path, entry->name, sizeof(entry->name));
    arfs.entry_count++;
    return 1;
}

static int parse_directory_line(const char *cursor, const char *line_end) {
    const char *path = 0;
    size_t path_length = 0;

    (void)next_token(cursor, line_end, &path, &path_length);
    return add_directory_entry(path, path_length);
}

static int parse_file_line(const char *cursor, const char *line_end) {
    const char *path = 0;
    const char *type = 0;
    const char *lba = 0;
    const char *size = 0;
    size_t path_length = 0;
    size_t type_length = 0;
    size_t lba_length = 0;
    size_t size_length = 0;
    uint64_t data_lba = 0;
    uint64_t size_bytes = 0;

    cursor = next_token(cursor, line_end, &path, &path_length);
    cursor = next_token(cursor, line_end, &type, &type_length);
    cursor = next_token(cursor, line_end, &lba, &lba_length);
    (void)next_token(cursor, line_end, &size, &size_length);

    if (!parse_decimal_token(lba, lba_length, &data_lba) ||
        !parse_decimal_token(size, size_length, &size_bytes)) {
        return 0;
    }

    return add_file_entry(
        path,
        path_length,
        type,
        type_length,
        data_lba,
        size_bytes
    );
}

static int parse_manifest(size_t manifest_size) {
    size_t cursor = 0;

    arfs.entry_count = 0;

    while (cursor < manifest_size && arfs.manifest[cursor] != '\0') {
        while (cursor < manifest_size &&
               (arfs.manifest[cursor] == '\n' || arfs.manifest[cursor] == '\r')) {
            cursor++;
        }

        if (cursor >= manifest_size || arfs.manifest[cursor] == '\0') {
            break;
        }

        const size_t line_start = cursor;

        while (cursor < manifest_size &&
               arfs.manifest[cursor] != '\n' &&
               arfs.manifest[cursor] != '\r' &&
               arfs.manifest[cursor] != '\0') {
            cursor++;
        }

        const char *line = (const char *)&arfs.manifest[line_start];
        const char *line_end = (const char *)&arfs.manifest[cursor];

        if (line >= line_end) {
            continue;
        }

        if (line[0] == 'D' && line + 1 < line_end && line[1] == ' ') {
            if (!parse_directory_line(line + 2, line_end)) {
                return 0;
            }
        } else if (line[0] == 'F' && line + 1 < line_end && line[1] == ' ') {
            if (!parse_file_line(line + 2, line_end)) {
                return 0;
            }
        } else {
            return 0;
        }
    }

    return arfs.entry_count > 0U;
}

static int parse_key_decimal(
    const uint8_t *sector,
    const char *key,
    uint64_t *value
) {
    const size_t key_length = string_length(key);

    for (size_t index = 0; index + key_length < arfs_sector_size; index++) {
        if (!starts_with((const char *)&sector[index], key)) {
            continue;
        }

        size_t value_start = index + key_length;
        size_t value_end = value_start;

        while (value_end < arfs_sector_size &&
               sector[value_end] >= '0' &&
               sector[value_end] <= '9') {
            value_end++;
        }

        return parse_decimal_token(
            (const char *)&sector[value_start],
            value_end - value_start,
            value
        );
    }

    return 0;
}

static int entry_is_child_of(const struct arfs_entry *entry, const char *path) {
    if (string_equals(path, "/")) {
        const char *tail = &entry->path[1];

        if (tail[0] == '\0') {
            return 0;
        }

        for (size_t index = 0; tail[index] != '\0'; index++) {
            if (tail[index] == '/') {
                return 0;
            }
        }

        return 1;
    }

    const size_t path_length = string_length(path);

    if (!starts_with(entry->path, path) || entry->path[path_length] != '/') {
        return 0;
    }

    const char *tail = &entry->path[path_length + 1U];

    if (tail[0] == '\0') {
        return 0;
    }

    for (size_t index = 0; tail[index] != '\0'; index++) {
        if (tail[index] == '/') {
            return 0;
        }
    }

    return 1;
}

static struct arfs_entry *find_entry(const char *path) {
    for (size_t index = 0; index < arfs.entry_count; index++) {
        if (string_equals(arfs.entries[index].path, path)) {
            return &arfs.entries[index];
        }
    }

    return 0;
}

static int arfs_owner_note_configured(void) {
    return arfs.writable_state_lba != 0U &&
        arfs.owner_note_lba != 0U &&
        arfs.owner_note_capacity > 0U &&
        arfs.owner_note_capacity <= arfs_write_buffer_capacity;
}

static int arfs_list(
    void *context,
    const char *path,
    struct arwill_fs_listing *listing
) {
    (void)context;

    size_t count = 0;

    if (!string_equals(path, "/")) {
        const struct arfs_entry *directory = find_entry(path);

        if (directory == 0 || directory->kind != arfs_entry_directory) {
            return 0;
        }
    }

    for (size_t index = 0; index < arfs.entry_count; index++) {
        const struct arfs_entry *entry = &arfs.entries[index];

        if (!entry_is_child_of(entry, path)) {
            continue;
        }

        if (count >= arfs_max_listing_entries) {
            return 0;
        }

        arfs.listing_entries[count].name = entry->name;
        arfs.listing_entries[count].size_bytes = entry->size_bytes;
        arfs.listing_entries[count].type =
            entry->kind == arfs_entry_directory
                ? arwill_fs_entry_directory
                : arwill_fs_entry_file;
        count++;
    }

    listing->entries = arfs.listing_entries;
    listing->count = count;
    return 1;
}

static int arfs_read_file(
    void *context,
    const char *path,
    struct arwill_fs_file *file
) {
    (void)context;

    const struct arfs_entry *entry = find_entry(path);

    if (entry == 0 || entry->kind != arfs_entry_file) {
        return 0;
    }

    file->type = entry->file_type;
    file->size_bytes = entry->size_bytes;
    file->contents = 0;

    if (entry->size_bytes == 0U) {
        arfs.file_buffer[0] = '\0';
        file->contents = arfs.file_buffer;
        return 1;
    }

    if (entry->size_bytes >= arfs_file_buffer_capacity) {
        return 0;
    }

    const uint64_t sector_count =
        (entry->size_bytes + (uint64_t)arfs_sector_size - 1U) / (uint64_t)arfs_sector_size;

    if (sector_count == 0U || sector_count > UINT32_MAX) {
        return 0;
    }

    if (!arwill_block_read(
            arfs.block_device,
            entry->data_lba,
            (uint32_t)sector_count,
            (uint8_t *)arfs.file_buffer,
            sizeof(arfs.file_buffer)
        )) {
        return 0;
    }

    arfs.file_buffer[entry->size_bytes] = '\0';
    file->contents = arfs.file_buffer;
    return 1;
}

static void clear_write_buffer(void) {
    for (size_t index = 0; index < sizeof(arfs.write_buffer); index++) {
        arfs.write_buffer[index] = 0;
    }
}

static int write_decimal(uint8_t *buffer, size_t capacity, size_t *offset, uint64_t value) {
    char reversed[20];
    size_t length = 0;

    do {
        reversed[length] = (char)('0' + (value % 10U));
        length++;
        value = value / 10U;
    } while (value != 0U && length < sizeof(reversed));

    if (*offset + length >= capacity) {
        return 0;
    }

    for (size_t index = 0; index < length; index++) {
        buffer[*offset] = (uint8_t)reversed[length - index - 1U];
        *offset = *offset + 1U;
    }

    return 1;
}

static int write_text(uint8_t *buffer, size_t capacity, size_t *offset, const char *text) {
    size_t index = 0;

    while (text[index] != '\0') {
        if (*offset + 1U >= capacity) {
            return 0;
        }

        buffer[*offset] = (uint8_t)text[index];
        *offset = *offset + 1U;
        index++;
    }

    return 1;
}

static int persist_owner_note_size(uint64_t size_bytes) {
    size_t offset = 0;

    clear_write_buffer();

    if (!write_text(arfs.write_buffer, sizeof(arfs.write_buffer), &offset, "owner_note_size=") ||
        !write_decimal(arfs.write_buffer, sizeof(arfs.write_buffer), &offset, size_bytes) ||
        !write_text(arfs.write_buffer, sizeof(arfs.write_buffer), &offset, "\n")) {
        return 0;
    }

    return arwill_block_write(
        arfs.block_device,
        arfs.writable_state_lba,
        1,
        arfs.write_buffer,
        sizeof(arfs.write_buffer)
    );
}

static int arfs_write_file(
    void *context,
    const char *path,
    const char *contents
) {
    (void)context;

    if (!string_equals(path, "/owner/note") || !arfs_owner_note_configured()) {
        return 0;
    }

    struct arfs_entry *entry = find_entry(path);
    const size_t contents_length = string_length(contents);

    if (entry == 0 || entry->kind != arfs_entry_file || entry->file_type != arwill_fs_file_text) {
        return 0;
    }

    if ((uint64_t)contents_length >= arfs.owner_note_capacity) {
        return 0;
    }

    clear_write_buffer();

    for (size_t index = 0; index < contents_length; index++) {
        arfs.write_buffer[index] = (uint8_t)contents[index];
    }

    if (!arwill_block_write(
            arfs.block_device,
            arfs.owner_note_lba,
            1,
            arfs.write_buffer,
            sizeof(arfs.write_buffer)
        )) {
        return 0;
    }

    if (!persist_owner_note_size((uint64_t)contents_length)) {
        return 0;
    }

    entry->data_lba = arfs.owner_note_lba;
    entry->size_bytes = (uint64_t)contents_length;
    return 1;
}

static int refresh_owner_note_state(void) {
    uint8_t state[arfs_sector_size];
    uint64_t size_bytes = 0;
    struct arfs_entry *entry = 0;

    if (!arfs_owner_note_configured()) {
        return 1;
    }

    entry = find_entry("/owner/note");

    if (entry == 0 || entry->kind != arfs_entry_file) {
        return 0;
    }

    if (!arwill_block_read(
            arfs.block_device,
            arfs.writable_state_lba,
            1,
            state,
            sizeof(state)
        )) {
        return 0;
    }

    if (!parse_key_decimal(state, "owner_note_size=", &size_bytes)) {
        return 0;
    }

    if (size_bytes >= arfs.owner_note_capacity) {
        return 0;
    }

    entry->data_lba = arfs.owner_note_lba;
    entry->size_bytes = size_bytes;
    return 1;
}

static const struct arwill_filesystem arfs_filesystem = {
    .name = "arfs writable owner note",
    .context = &arfs,
    .list = arfs_list,
    .read_file = arfs_read_file,
    .write_file = arfs_write_file,
};

const struct arwill_filesystem *arwill_arfs_mount(
    const struct arwill_block_device *block_device
) {
    uint8_t superblock[arfs_sector_size];
    uint64_t manifest_lba = 0;
    uint64_t manifest_sectors = 0;

    arfs.block_device = block_device;
    arfs.entry_count = 0;
    arfs.writable_state_lba = 0;
    arfs.owner_note_lba = 0;
    arfs.owner_note_capacity = 0;

    if (block_device == 0 || block_device->sector_size != arfs_sector_size) {
        return 0;
    }

    if (!arwill_block_read(
            block_device,
            arfs_superblock_lba,
            1,
            superblock,
            sizeof(superblock)
        )) {
        return 0;
    }

    if (!starts_with((const char *)superblock, "ARFS1\n")) {
        return 0;
    }

    if (!parse_key_decimal(superblock, "manifest_lba=", &manifest_lba) ||
        !parse_key_decimal(superblock, "manifest_sectors=", &manifest_sectors)) {
        return 0;
    }

    (void)parse_key_decimal(superblock, "writable_state_lba=", &arfs.writable_state_lba);
    (void)parse_key_decimal(superblock, "owner_note_lba=", &arfs.owner_note_lba);
    (void)parse_key_decimal(superblock, "owner_note_capacity=", &arfs.owner_note_capacity);

    if (manifest_sectors == 0U || manifest_sectors > arfs_max_manifest_sectors) {
        return 0;
    }

    if (!arwill_block_read(
            block_device,
            manifest_lba,
            (uint32_t)manifest_sectors,
            arfs.manifest,
            sizeof(arfs.manifest)
        )) {
        return 0;
    }

    if (!parse_manifest((size_t)manifest_sectors * arfs_sector_size)) {
        arfs.entry_count = 0;
        return 0;
    }

    if (!refresh_owner_note_state()) {
        arfs.entry_count = 0;
        return 0;
    }

    return &arfs_filesystem;
}
