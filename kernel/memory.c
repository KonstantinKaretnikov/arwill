#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/memory.h>

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value - (value % alignment);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    const uint64_t remainder = value % alignment;

    if (remainder == 0U) {
        return value;
    }

    if (value > UINT64_MAX - (alignment - remainder)) {
        return UINT64_MAX;
    }

    return value + (alignment - remainder);
}

static uint64_t saturating_add(uint64_t left, uint64_t right) {
    if (left > UINT64_MAX - right) {
        return UINT64_MAX;
    }

    return left + right;
}

static void physical_allocator_init(
    struct arwill_physical_allocator *allocator,
    const struct arwill_memory_region *regions,
    size_t region_count
) {
    allocator->range_count = 0;
    allocator->total_pages = 0;
    allocator->free_pages = 0;
    allocator->allocated_pages = 0;
    allocator->allocation_count = 0;

    for (size_t index = 0; index < region_count; index++) {
        const struct arwill_memory_region *region = &regions[index];

        if (region->type != arwill_memory_region_usable) {
            continue;
        }

        const uint64_t start = align_up(region->base, ARWILL_MEMORY_PAGE_SIZE);
        const uint64_t end = align_down(
            saturating_add(region->base, region->length),
            ARWILL_MEMORY_PAGE_SIZE
        );

        if (end <= start) {
            continue;
        }

        if (allocator->range_count >= ARWILL_PHYSICAL_ALLOCATOR_RANGE_CAPACITY) {
            continue;
        }

        const uint64_t pages = (end - start) / ARWILL_MEMORY_PAGE_SIZE;
        struct arwill_physical_allocator_range *range =
            &allocator->ranges[allocator->range_count];

        range->next = start;
        range->end = end;
        allocator->range_count++;
        allocator->total_pages += pages;
        allocator->free_pages += pages;
    }
}

void arwill_memory_init(
    struct arwill_memory *memory,
    const struct arwill_memory_region *regions,
    size_t region_count,
    int truncated
) {
    if (memory == 0) {
        return;
    }

    if (regions == 0) {
        region_count = 0;
    }

    memory->map.regions = regions;
    memory->map.count = region_count;
    memory->map.truncated = truncated;

    physical_allocator_init(&memory->physical_allocator, regions, region_count);
}

const struct arwill_memory_map *arwill_memory_map(const struct arwill_memory *memory) {
    if (memory == 0) {
        return 0;
    }

    return &memory->map;
}

const char *arwill_memory_region_type_name(enum arwill_memory_region_type type) {
    switch (type) {
        case arwill_memory_region_usable:
            return "usable";
        case arwill_memory_region_reserved:
            return "reserved";
        case arwill_memory_region_acpi_reclaimable:
            return "acpi reclaimable";
        case arwill_memory_region_acpi_nvs:
            return "acpi nvs";
        case arwill_memory_region_bad_memory:
            return "bad memory";
        case arwill_memory_region_bootloader_reclaimable:
            return "bootloader reclaimable";
        case arwill_memory_region_executable_and_modules:
            return "kernel/modules";
        case arwill_memory_region_framebuffer:
            return "framebuffer";
        case arwill_memory_region_reserved_mapped:
            return "reserved mapped";
        case arwill_memory_region_unknown:
            return "unknown";
    }

    return "unknown";
}

void arwill_physical_allocator_stats(
    const struct arwill_memory *memory,
    struct arwill_physical_allocator_stats *stats
) {
    if (stats == 0) {
        return;
    }

    stats->page_size = ARWILL_MEMORY_PAGE_SIZE;
    stats->total_pages = 0;
    stats->free_pages = 0;
    stats->allocated_pages = 0;
    stats->allocation_count = 0;
    stats->range_count = 0;

    if (memory == 0) {
        return;
    }

    stats->total_pages = memory->physical_allocator.total_pages;
    stats->free_pages = memory->physical_allocator.free_pages;
    stats->allocated_pages = memory->physical_allocator.allocated_pages;
    stats->allocation_count = memory->physical_allocator.allocation_count;
    stats->range_count = memory->physical_allocator.range_count;
}

int arwill_physical_allocate_page(struct arwill_memory *memory, uint64_t *physical_address) {
    if (memory == 0 || physical_address == 0) {
        return 0;
    }

    struct arwill_physical_allocator *allocator = &memory->physical_allocator;

    for (size_t index = 0; index < allocator->range_count; index++) {
        struct arwill_physical_allocator_range *range = &allocator->ranges[index];

        if (range->next >= range->end) {
            continue;
        }

        *physical_address = range->next;
        range->next += ARWILL_MEMORY_PAGE_SIZE;
        allocator->free_pages--;
        allocator->allocated_pages++;
        allocator->allocation_count++;
        return 1;
    }

    return 0;
}
