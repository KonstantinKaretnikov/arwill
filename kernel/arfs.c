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
    arfs_write_buffer_capacity = 2048
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
    uint64_t manifest_lba;
    uint64_t manifest_sectors;
    uint64_t data_lba;
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

static int path_is_valid(const char *path) {
    const size_t length = string_length(path);
    size_t segment_length = 0;

    if (length < 2U || length >= arfs_max_path_length || path[0] != '/' ||
        path[length - 1U] == '/') {
        return 0;
    }

    for (size_t index = 1; index < length; index++) {
        if (path[index] == ' ' || (path[index] == '/' && path[index - 1U] == '/')) {
            return 0;
        }

        if (path[index] == '/') {
            if (segment_length == 0U || segment_length >= arfs_max_name_length) {
                return 0;
            }
            segment_length = 0;
        } else {
            segment_length++;
        }
    }

    return segment_length > 0U && segment_length < arfs_max_name_length;
}

static int parent_directory_exists(const char *path) {
    char parent[arfs_max_path_length];
    size_t slash = 0;
    const size_t length = string_length(path);

    for (size_t index = 1; index < length; index++) {
        if (path[index] == '/') {
            slash = index;
        }
    }

    if (slash == 0U) {
        return 1;
    }

    if (!copy_token(parent, sizeof(parent), path, slash)) {
        return 0;
    }

    const struct arfs_entry *entry = find_entry(parent);
    return entry != 0 && entry->kind == arfs_entry_directory;
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

static int persist_manifest(void) {
    size_t offset = 0;

    for (size_t index = 0; index < sizeof(arfs.manifest); index++) {
        arfs.manifest[index] = 0;
    }

    for (size_t index = 0; index < arfs.entry_count; index++) {
        const struct arfs_entry *entry = &arfs.entries[index];

        if (!write_text(arfs.manifest, sizeof(arfs.manifest), &offset,
                entry->kind == arfs_entry_directory ? "D " : "F ") ||
            !write_text(arfs.manifest, sizeof(arfs.manifest), &offset, entry->path)) {
            return 0;
        }

        if (entry->kind == arfs_entry_file) {
            if (!write_text(arfs.manifest, sizeof(arfs.manifest), &offset,
                    entry->file_type == arwill_fs_file_text ? " text " : " binary ") ||
                !write_decimal(arfs.manifest, sizeof(arfs.manifest), &offset, entry->data_lba) ||
                !write_text(arfs.manifest, sizeof(arfs.manifest), &offset, " ") ||
                !write_decimal(arfs.manifest, sizeof(arfs.manifest), &offset, entry->size_bytes)) {
                return 0;
            }
        }

        if (!write_text(arfs.manifest, sizeof(arfs.manifest), &offset, "\n")) {
            return 0;
        }
    }

    return arwill_block_write(
        arfs.block_device,
        arfs.manifest_lba,
        (uint32_t)arfs.manifest_sectors,
        arfs.manifest,
        sizeof(arfs.manifest)
    );
}

static uint64_t entry_sector_count(const struct arfs_entry *entry) {
    if (entry->kind != arfs_entry_file || entry->size_bytes == 0U) {
        return 0;
    }

    return (entry->size_bytes + (uint64_t)arfs_sector_size - 1U) /
        (uint64_t)arfs_sector_size;
}

static int sector_range_is_free(uint64_t first, uint64_t count) {
    for (size_t index = 0; index < arfs.entry_count; index++) {
        const struct arfs_entry *entry = &arfs.entries[index];
        const uint64_t used = entry_sector_count(entry);

        if (used != 0U && first < entry->data_lba + used && entry->data_lba < first + count) {
            return 0;
        }
    }

    return 1;
}

static int allocate_sectors(uint64_t count, uint64_t *first) {
    if (count == 0U || first == 0 || arfs.block_device == 0 ||
        count > arfs.block_device->sector_count ||
        arfs.data_lba > arfs.block_device->sector_count - count) {
        return 0;
    }

    for (uint64_t candidate = arfs.data_lba;
         candidate <= arfs.block_device->sector_count - count;
         candidate++) {
        if (sector_range_is_free(candidate, count)) {
            *first = candidate;
            return 1;
        }
    }

    return 0;
}

static int arfs_write_bytes(
    void *context,
    const char *path,
    enum arwill_fs_file_type type,
    const uint8_t *contents,
    size_t size
) {
    (void)context;

    if (!path_is_valid(path) || !parent_directory_exists(path) ||
        (type != arwill_fs_file_text && type != arwill_fs_file_binary) ||
        size >= arfs_file_buffer_capacity || (contents == 0 && size != 0U)) {
        return 0;
    }

    struct arfs_entry *entry = find_entry(path);
    const int is_new = entry == 0;
    struct arfs_entry previous;
    uint64_t data_lba = 0;
    const uint64_t sectors = size == 0U ? 0U :
        ((uint64_t)size + (uint64_t)arfs_sector_size - 1U) / (uint64_t)arfs_sector_size;

    if (!is_new && entry->kind != arfs_entry_file) {
        return 0;
    }

    if (is_new) {
        if (arfs.entry_count >= arfs_max_entries) {
            return 0;
        }
        entry = &arfs.entries[arfs.entry_count];
    } else {
        previous = *entry;
    }

    if (sectors != 0U) {
        const uint64_t current_sectors = is_new ? 0U : entry_sector_count(entry);

        if (current_sectors >= sectors) {
            data_lba = entry->data_lba;
        } else if (!allocate_sectors(sectors, &data_lba)) {
            return 0;
        }

        clear_write_buffer();
        for (size_t index = 0; index < size; index++) {
            arfs.write_buffer[index] = contents[index];
        }

        if (!arwill_block_write(
                arfs.block_device,
                data_lba,
                (uint32_t)sectors,
                arfs.write_buffer,
                sizeof(arfs.write_buffer))) {
            return 0;
        }
    }

    entry->kind = arfs_entry_file;
    entry->file_type = type;
    entry->data_lba = data_lba;
    entry->size_bytes = (uint64_t)size;
    if (is_new && (!copy_token(entry->path, sizeof(entry->path), path, string_length(path)))) {
        return 0;
    }
    derive_name(entry->path, entry->name, sizeof(entry->name));

    if (is_new) {
        arfs.entry_count++;
    }

    if (!persist_manifest()) {
        if (is_new) {
            arfs.entry_count--;
        } else {
            *entry = previous;
        }
        return 0;
    }

    return 1;
}

static int arfs_write_file(void *context, const char *path, const char *contents) {
    if (!string_equals(path, "/owner/note")) {
        return 0;
    }

    return arfs_write_bytes(
        context,
        path,
        arwill_fs_file_text,
        (const uint8_t *)contents,
        string_length(contents)
    );
}

static int arfs_create_directory(void *context, const char *path) {
    (void)context;

    if (!path_is_valid(path) || find_entry(path) != 0 || !parent_directory_exists(path) ||
        !add_directory_entry(path, string_length(path))) {
        return 0;
    }

    if (!persist_manifest()) {
        arfs.entry_count--;
        return 0;
    }

    return 1;
}

static int arfs_remove(void *context, const char *path) {
    (void)context;
    size_t remove_index = arfs.entry_count;

    for (size_t index = 0; index < arfs.entry_count; index++) {
        if (string_equals(arfs.entries[index].path, path)) {
            remove_index = index;
            break;
        }
    }

    if (remove_index == arfs.entry_count) {
        return 0;
    }

    if (arfs.entries[remove_index].kind == arfs_entry_directory) {
        for (size_t index = 0; index < arfs.entry_count; index++) {
            if (entry_is_child_of(&arfs.entries[index], path)) {
                return 0;
            }
        }
    }

    const struct arfs_entry removed = arfs.entries[remove_index];
    for (size_t index = remove_index; index + 1U < arfs.entry_count; index++) {
        arfs.entries[index] = arfs.entries[index + 1U];
    }
    arfs.entry_count--;

    if (!persist_manifest()) {
        for (size_t index = arfs.entry_count; index > remove_index; index--) {
            arfs.entries[index] = arfs.entries[index - 1U];
        }
        arfs.entries[remove_index] = removed;
        arfs.entry_count++;
        return 0;
    }

    return 1;
}

static const struct arwill_filesystem arfs_filesystem = {
    .name = "arfs mutable",
    .context = &arfs,
    .list = arfs_list,
    .read_file = arfs_read_file,
    .write_file = arfs_write_file,
    .create_directory = arfs_create_directory,
    .write_bytes = arfs_write_bytes,
    .remove = arfs_remove,
};

const struct arwill_filesystem *arwill_arfs_mount(
    const struct arwill_block_device *block_device
) {
    uint8_t superblock[arfs_sector_size];
    uint64_t manifest_lba = 0;
    uint64_t manifest_sectors = 0;

    arfs.block_device = block_device;
    arfs.entry_count = 0;
    arfs.manifest_lba = 0;
    arfs.manifest_sectors = 0;
    arfs.data_lba = 0;

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

    if (!starts_with((const char *)superblock, "ARFS2\n")) {
        return 0;
    }

    if (!parse_key_decimal(superblock, "manifest_lba=", &manifest_lba) ||
        !parse_key_decimal(superblock, "manifest_sectors=", &manifest_sectors)) {
        return 0;
    }

    if (!parse_key_decimal(superblock, "data_lba=", &arfs.data_lba)) {
        return 0;
    }

    if (arfs.data_lba >= block_device->sector_count) {
        return 0;
    }

    arfs.manifest_lba = manifest_lba;
    arfs.manifest_sectors = manifest_sectors;
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

    return &arfs_filesystem;
}
