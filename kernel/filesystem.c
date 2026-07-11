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
