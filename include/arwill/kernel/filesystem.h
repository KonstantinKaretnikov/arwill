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

enum arwill_fs_file_type {
    arwill_fs_file_text,
    arwill_fs_file_binary
};

struct arwill_fs_file {
    enum arwill_fs_file_type type;
    const char *contents;
    uint64_t size_bytes;
};

struct arwill_filesystem {
    const char *name;
    void *context;
    int (*list)(void *context, const char *path, struct arwill_fs_listing *listing);
    int (*read_file)(void *context, const char *path, struct arwill_fs_file *file);
    int (*write_file)(void *context, const char *path, const char *contents);
};

int arwill_filesystem_list(
    const struct arwill_filesystem *filesystem,
    const char *path,
    struct arwill_fs_listing *listing
);

int arwill_filesystem_read_file(
    const struct arwill_filesystem *filesystem,
    const char *path,
    struct arwill_fs_file *file
);

int arwill_filesystem_write_file(
    const struct arwill_filesystem *filesystem,
    const char *path,
    const char *contents
);

#endif
