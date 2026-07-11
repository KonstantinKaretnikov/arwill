#ifndef ARWILL_KERNEL_FILESYSTEM_H
#define ARWILL_KERNEL_FILESYSTEM_H

#include <stddef.h>
#include <stdint.h>

enum arwill_fs_entry_type {
    arwill_fs_entry_directory,
    arwill_fs_entry_file
};

struct arwill_fs_entry {
    const char *name;
    enum arwill_fs_entry_type type;
    uint64_t size_bytes;
};

struct arwill_fs_listing {
    const struct arwill_fs_entry *entries;
    size_t count;
};

struct arwill_filesystem {
    void *context;
    int (*list)(void *context, const char *path, struct arwill_fs_listing *listing);
};

int arwill_filesystem_list(
    const struct arwill_filesystem *filesystem,
    const char *path,
    struct arwill_fs_listing *listing
);

#endif
