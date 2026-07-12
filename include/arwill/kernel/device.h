#ifndef ARWILL_KERNEL_DEVICE_H
#define ARWILL_KERNEL_DEVICE_H

#include <stddef.h>

enum {
    arwill_device_registry_capacity = 16
};

enum arwill_device_kind {
    arwill_device_kind_console,
    arwill_device_kind_input,
    arwill_device_kind_filesystem,
    arwill_device_kind_block,
    arwill_device_kind_memory,
    arwill_device_kind_power,
    arwill_device_kind_interrupts,
    arwill_device_kind_user_runtime
};

struct arwill_device_entry {
    const char *name;
    enum arwill_device_kind kind;
    const char *driver;
    const char *status;
};

struct arwill_device_registry {
    struct arwill_device_entry entries[arwill_device_registry_capacity];
    size_t count;
};

void arwill_device_registry_init(struct arwill_device_registry *registry);

int arwill_device_register(
    struct arwill_device_registry *registry,
    const char *name,
    enum arwill_device_kind kind,
    const char *driver,
    const char *status
);

const struct arwill_device_entry *arwill_device_entries(
    const struct arwill_device_registry *registry,
    size_t *count
);

const char *arwill_device_kind_name(enum arwill_device_kind kind);

#endif
