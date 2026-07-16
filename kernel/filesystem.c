#include <stddef.h>

#include <arwill/kernel/filesystem.h>

int arwill_filesystem_list(
    const struct arwill_filesystem *filesystem,
    const char *path,
    struct arwill_fs_listing *listing
) {
    if (listing == 0) {
        return 0;
    }

    listing->entries = 0;
    listing->count = 0;

    if (filesystem == 0 || filesystem->list == 0 || path == 0) {
        return 0;
    }

    return filesystem->list(filesystem->context, path, listing);
}

int arwill_filesystem_read_file(
    const struct arwill_filesystem *filesystem,
    const char *path,
    struct arwill_fs_file *file
) {
    if (file == 0) {
        return 0;
    }

    file->type = arwill_fs_file_text;
    file->contents = 0;
    file->size_bytes = 0;

    if (filesystem == 0 || filesystem->read_file == 0 || path == 0) {
        return 0;
    }

    return filesystem->read_file(filesystem->context, path, file);
}

int arwill_filesystem_create_directory(
    const struct arwill_filesystem *filesystem,
    const char *path
) {
    if (filesystem == 0 || filesystem->create_directory == 0 || path == 0) {
        return 0;
    }

    return filesystem->create_directory(filesystem->context, path);
}

int arwill_filesystem_write_bytes(
    const struct arwill_filesystem *filesystem,
    const char *path,
    enum arwill_fs_file_type type,
    const uint8_t *contents,
    size_t size
) {
    if (filesystem == 0 || filesystem->write_bytes == 0 || path == 0 ||
        (contents == 0 && size != 0U)) {
        return 0;
    }

    return filesystem->write_bytes(filesystem->context, path, type, contents, size);
}

int arwill_filesystem_remove(
    const struct arwill_filesystem *filesystem,
    const char *path
) {
    if (filesystem == 0 || filesystem->remove == 0 || path == 0) {
        return 0;
    }

    return filesystem->remove(filesystem->context, path);
}

int arwill_filesystem_storage_stats(
    const struct arwill_filesystem *filesystem,
    struct arwill_fs_storage_stats *stats
) {
    if (stats == 0) {
        return 0;
    }

    stats->entries_used = 0;
    stats->entries_capacity = 0;
    stats->manifest_sectors = 0;
    stats->data_sectors = 0;
    stats->used_data_sectors = 0;
    stats->free_data_sectors = 0;
    stats->largest_free_run_sectors = 0;
    stats->max_path_bytes = 0;
    stats->max_file_bytes = 0;

    if (filesystem == 0 || filesystem->storage_stats == 0) {
        return 0;
    }

    return filesystem->storage_stats(filesystem->context, stats);
}
