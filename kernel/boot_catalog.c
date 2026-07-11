#include <stddef.h>

#include <arwill/kernel/boot_catalog.h>
#include <arwill/kernel/filesystem.h>

static const struct arwill_fs_entry root_entries[] = {
    { .name = "boot", .type = arwill_fs_entry_directory, .size_bytes = 0 },
    { .name = "system", .type = arwill_fs_entry_directory, .size_bytes = 0 },
};

static const struct arwill_fs_entry boot_entries[] = {
    { .name = "kernel.elf", .type = arwill_fs_entry_file, .size_bytes = 0 },
    { .name = "limine", .type = arwill_fs_entry_directory, .size_bytes = 0 },
};

static const struct arwill_fs_entry limine_entries[] = {
    { .name = "limine.conf", .type = arwill_fs_entry_file, .size_bytes = 0 },
    { .name = "limine-bios.sys", .type = arwill_fs_entry_file, .size_bytes = 0 },
};

static const struct arwill_fs_entry system_entries[] = {
    { .name = "identity", .type = arwill_fs_entry_file, .size_bytes = 0 },
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

static int listing_for(
    const struct arwill_fs_entry *entries,
    size_t count,
    struct arwill_fs_listing *listing
) {
    listing->entries = entries;
    listing->count = count;

    return 1;
}

static int boot_catalog_list(
    void *context,
    const char *path,
    struct arwill_fs_listing *listing
) {
    (void)context;

    if (string_equals(path, "/") || string_equals(path, "")) {
        return listing_for(
            root_entries,
            sizeof(root_entries) / sizeof(root_entries[0]),
            listing
        );
    }

    if (string_equals(path, "/boot") || string_equals(path, "/boot/")) {
        return listing_for(
            boot_entries,
            sizeof(boot_entries) / sizeof(boot_entries[0]),
            listing
        );
    }

    if (string_equals(path, "/boot/limine") || string_equals(path, "/boot/limine/")) {
        return listing_for(
            limine_entries,
            sizeof(limine_entries) / sizeof(limine_entries[0]),
            listing
        );
    }

    if (string_equals(path, "/system") || string_equals(path, "/system/")) {
        return listing_for(
            system_entries,
            sizeof(system_entries) / sizeof(system_entries[0]),
            listing
        );
    }

    return 0;
}

static const struct arwill_filesystem boot_catalog = {
    .context = 0,
    .list = boot_catalog_list,
};

const struct arwill_filesystem *arwill_boot_catalog_filesystem(void) {
    return &boot_catalog;
}
