#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <arwill/kernel/block_device.h>

enum {
    sector_size = 4,
    parent_sectors = 8,
};

struct memory_device {
    uint8_t bytes[sector_size * parent_sectors];
};

static int memory_read(
    void *context,
    uint64_t lba,
    uint32_t sector_count,
    uint8_t *buffer,
    size_t buffer_size
) {
    struct memory_device *memory = (struct memory_device *)context;
    const size_t byte_count = (size_t)sector_count * sector_size;
    const size_t offset = (size_t)lba * sector_size;

    if (buffer_size < byte_count) {
        return 0;
    }

    for (size_t index = 0; index < byte_count; index++) {
        buffer[index] = memory->bytes[offset + index];
    }
    return 1;
}

static int memory_write(
    void *context,
    uint64_t lba,
    uint32_t sector_count,
    const uint8_t *buffer,
    size_t buffer_size
) {
    struct memory_device *memory = (struct memory_device *)context;
    const size_t byte_count = (size_t)sector_count * sector_size;
    const size_t offset = (size_t)lba * sector_size;

    if (buffer_size < byte_count) {
        return 0;
    }

    for (size_t index = 0; index < byte_count; index++) {
        memory->bytes[offset + index] = buffer[index];
    }
    return 1;
}

static int require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "block device test failed: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void) {
    struct memory_device memory = {0};
    struct arwill_block_device parent = {
        .context = &memory,
        .name = "memory",
        .sector_size = sector_size,
        .sector_count = parent_sectors,
        .read = memory_read,
        .write = memory_write,
    };
    struct arwill_block_region region = {0};
    uint8_t read_buffer[sector_size] = {0};
    const uint8_t write_buffer[sector_size] = {9, 8, 7, 6};

    memory.bytes[2U * sector_size] = 1;
    memory.bytes[2U * sector_size + 1U] = 2;
    memory.bytes[2U * sector_size + 2U] = 3;
    memory.bytes[2U * sector_size + 3U] = 4;

    if (!require(arwill_block_region_init(&region, &parent, "region", 2, 3),
                 "valid region init") ||
        !require(arwill_block_read(&region.device, 0, 1, read_buffer, sizeof(read_buffer)),
                 "translated read") ||
        !require(read_buffer[0] == 1U && read_buffer[3] == 4U,
                 "read came from parent LBA") ||
        !require(arwill_block_write(&region.device, 2, 1, write_buffer, sizeof(write_buffer)),
                 "translated write") ||
        !require(memory.bytes[4U * sector_size] == 9U &&
                 memory.bytes[4U * sector_size + 3U] == 6U,
                 "write reached parent LBA") ||
        !require(!arwill_block_read(&region.device, 3, 1, read_buffer, sizeof(read_buffer)),
                 "read cannot cross region end") ||
        !require(!arwill_block_region_init(&region, &parent, "bad", 7, 2),
                 "region cannot cross parent end") ||
        !require(arwill_block_region_device(&region) == 0,
                 "failed init invalidates old region")) {
        return 1;
    }

    puts("block device region test passed");
    return 0;
}
