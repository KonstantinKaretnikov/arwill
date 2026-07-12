#include <stddef.h>

#include <arwill/kernel/device.h>

void arwill_device_registry_init(struct arwill_device_registry *registry) {
    if (registry == 0) {
        return;
    }

    registry->count = 0;

    for (size_t index = 0; index < arwill_device_registry_capacity; index++) {
        registry->entries[index].name = "";
        registry->entries[index].kind = arwill_device_kind_console;
        registry->entries[index].driver = "";
        registry->entries[index].status = "";
    }
}

int arwill_device_register(
    struct arwill_device_registry *registry,
    const char *name,
    enum arwill_device_kind kind,
    const char *driver,
    const char *status
) {
    if (registry == 0 || name == 0 || driver == 0 || status == 0) {
        return 0;
    }

    if (registry->count >= arwill_device_registry_capacity) {
        return 0;
    }

    struct arwill_device_entry *entry = &registry->entries[registry->count];

    entry->name = name;
    entry->kind = kind;
    entry->driver = driver;
    entry->status = status;
    registry->count++;
    return 1;
}

const struct arwill_device_entry *arwill_device_entries(
    const struct arwill_device_registry *registry,
    size_t *count
) {
    if (count != 0) {
        *count = 0;
    }

    if (registry == 0) {
        return 0;
    }

    if (count != 0) {
        *count = registry->count;
    }

    return registry->entries;
}

const char *arwill_device_kind_name(enum arwill_device_kind kind) {
    switch (kind) {
        case arwill_device_kind_console:
            return "console";
        case arwill_device_kind_input:
            return "input";
        case arwill_device_kind_filesystem:
            return "filesystem";
        case arwill_device_kind_block:
            return "block";
        case arwill_device_kind_memory:
            return "memory";
        case arwill_device_kind_power:
            return "power";
        case arwill_device_kind_interrupts:
            return "interrupts";
        case arwill_device_kind_user_runtime:
            return "user";
    }

    return "unknown";
}
