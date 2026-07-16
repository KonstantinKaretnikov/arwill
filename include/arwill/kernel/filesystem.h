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

struct arwill_fs_storage_stats {
    size_t entries_used;
    size_t entries_capacity;
    uint64_t manifest_sectors;
    uint64_t data_sectors;
    uint64_t used_data_sectors;
    uint64_t free_data_sectors;
    uint64_t largest_free_run_sectors;
    size_t max_path_bytes;
    size_t max_file_bytes;
};

struct arwill_filesystem {
    const char *name;
    void *context;
    int (*list)(void *context, const char *path, struct arwill_fs_listing *listing);
    int (*read_file)(void *context, const char *path, struct arwill_fs_file *file);
    int (*create_directory)(void *context, const char *path);
    int (*write_bytes)(
        void *context,
        const char *path,
        enum arwill_fs_file_type type,
        const uint8_t *contents,
        size_t size
    );
    int (*remove)(void *context, const char *path);
    int (*storage_stats)(void *context, struct arwill_fs_storage_stats *stats);
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

int arwill_filesystem_create_directory(
    const struct arwill_filesystem *filesystem,
    const char *path
);

int arwill_filesystem_write_bytes(
    const struct arwill_filesystem *filesystem,
    const char *path,
    enum arwill_fs_file_type type,
    const uint8_t *contents,
    size_t size
);

int arwill_filesystem_remove(
    const struct arwill_filesystem *filesystem,
    const char *path
);

int arwill_filesystem_storage_stats(
    const struct arwill_filesystem *filesystem,
    struct arwill_fs_storage_stats *stats
);

#endif
