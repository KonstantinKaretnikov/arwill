#ifndef ARWILL_KERNEL_BLOCK_DEVICE_H
#define ARWILL_KERNEL_BLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>

struct arwill_block_device {
    void *context;
    const char *name;
    uint32_t sector_size;
    uint64_t sector_count;
    int (*read)(
        void *context,
        uint64_t lba,
        uint32_t sector_count,
        uint8_t *buffer,
        size_t buffer_size
    );
    int (*write)(
        void *context,
        uint64_t lba,
        uint32_t sector_count,
        const uint8_t *buffer,
        size_t buffer_size
    );
};

int arwill_block_read(
    const struct arwill_block_device *device,
    uint64_t lba,
    uint32_t sector_count,
    uint8_t *buffer,
    size_t buffer_size
);

int arwill_block_write(
    const struct arwill_block_device *device,
    uint64_t lba,
    uint32_t sector_count,
    const uint8_t *buffer,
    size_t buffer_size
);

#endif
