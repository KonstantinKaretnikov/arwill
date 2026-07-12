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

int arwill_filesystem_write_file(
    const struct arwill_filesystem *filesystem,
    const char *path,
    const char *contents
) {
    if (filesystem == 0 || filesystem->write_file == 0 || path == 0 || contents == 0) {
        return 0;
    }

    return filesystem->write_file(filesystem->context, path, contents);
}
