#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/block_device.h>

static int read_range_fits(
    const struct arwill_block_device *device,
    uint64_t lba,
    uint32_t sector_count
) {
    if (sector_count == 0U) {
        return 0;
    }

    if (lba > UINT64_MAX - (uint64_t)sector_count) {
        return 0;
    }

    return lba + (uint64_t)sector_count <= device->sector_count;
}

static int buffer_fits(
    const struct arwill_block_device *device,
    uint32_t sector_count,
    size_t buffer_size
) {
    const uint64_t required_size = (uint64_t)sector_count * (uint64_t)device->sector_size;

    if (device->sector_size == 0U || required_size > (uint64_t)SIZE_MAX) {
        return 0;
    }

    return (uint64_t)buffer_size >= required_size;
}

int arwill_block_read(
    const struct arwill_block_device *device,
    uint64_t lba,
    uint32_t sector_count,
    uint8_t *buffer,
    size_t buffer_size
) {
    if (device == 0 || device->read == 0 || buffer == 0) {
        return 0;
    }

    if (!read_range_fits(device, lba, sector_count)) {
        return 0;
    }

    if (!buffer_fits(device, sector_count, buffer_size)) {
        return 0;
    }

    return device->read(device->context, lba, sector_count, buffer, buffer_size);
}
