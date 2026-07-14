#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/block_device.h>

static int range_fits(
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

    if (!range_fits(device, lba, sector_count)) {
        return 0;
    }

    if (!buffer_fits(device, sector_count, buffer_size)) {
        return 0;
    }

    return device->read(device->context, lba, sector_count, buffer, buffer_size);
}

int arwill_block_write(
    const struct arwill_block_device *device,
    uint64_t lba,
    uint32_t sector_count,
    const uint8_t *buffer,
    size_t buffer_size
) {
    if (device == 0 || device->write == 0 || buffer == 0) {
        return 0;
    }

    if (!range_fits(device, lba, sector_count)) {
        return 0;
    }

    if (!buffer_fits(device, sector_count, buffer_size)) {
        return 0;
    }

    return device->write(device->context, lba, sector_count, buffer, buffer_size);
}

static int region_read(
    void *context,
    uint64_t lba,
    uint32_t sector_count,
    uint8_t *buffer,
    size_t buffer_size
) {
    const struct arwill_block_region *region =
        (const struct arwill_block_region *)context;

    if (region == 0 || region->parent == 0 ||
        lba > UINT64_MAX - region->first_lba) {
        return 0;
    }

    return arwill_block_read(
        region->parent,
        region->first_lba + lba,
        sector_count,
        buffer,
        buffer_size
    );
}

static int region_write(
    void *context,
    uint64_t lba,
    uint32_t sector_count,
    const uint8_t *buffer,
    size_t buffer_size
) {
    const struct arwill_block_region *region =
        (const struct arwill_block_region *)context;

    if (region == 0 || region->parent == 0 ||
        lba > UINT64_MAX - region->first_lba) {
        return 0;
    }

    return arwill_block_write(
        region->parent,
        region->first_lba + lba,
        sector_count,
        buffer,
        buffer_size
    );
}

int arwill_block_region_init(
    struct arwill_block_region *region,
    const struct arwill_block_device *parent,
    const char *name,
    uint64_t first_lba,
    uint64_t sector_count
) {
    if (region != 0) {
        region->parent = 0;
    }

    if (region == 0 || parent == 0 || name == 0 || sector_count == 0U ||
        first_lba > parent->sector_count ||
        sector_count > parent->sector_count - first_lba) {
        return 0;
    }

    region->parent = parent;
    region->first_lba = first_lba;
    region->device.context = region;
    region->device.name = name;
    region->device.sector_size = parent->sector_size;
    region->device.sector_count = sector_count;
    region->device.read = region_read;
    region->device.write = parent->write == 0 ? 0 : region_write;
    return 1;
}

const struct arwill_block_device *arwill_block_region_device(
    const struct arwill_block_region *region
) {
    if (region == 0 || region->parent == 0) {
        return 0;
    }

    return &region->device;
}
