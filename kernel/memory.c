#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/memory.h>

enum {
    heap_alignment = 16
};

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

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

static size_t align_size_up(size_t value, size_t alignment) {
    const size_t remainder = value % alignment;

    if (remainder == 0U) {
        return value;
    }

    if (value > SIZE_MAX - (alignment - remainder)) {
        return SIZE_MAX;
    }

    return value + (alignment - remainder);
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

    memory->kernel_heap.base = 0;
    memory->kernel_heap.size_bytes = 0;
    memory->kernel_heap.used_bytes = 0;
    memory->kernel_heap.allocation_count = 0;
    memory->kernel_heap.free_count = 0;
    memory->kernel_heap.failed_allocation_count = 0;
    memory->kernel_heap.initialized = 0;
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

static struct heap_block *heap_first_block(struct arwill_memory *memory) {
    return (struct heap_block *)memory->kernel_heap.base;
}

static size_t heap_payload_offset(void) {
    return align_size_up(sizeof(struct heap_block), heap_alignment);
}

static void heap_coalesce(struct arwill_memory *memory) {
    struct heap_block *block = heap_first_block(memory);

    while (block != 0 && block->next != 0) {
        if (block->free && block->next->free) {
            block->size += heap_payload_offset() + block->next->size;
            block->next = block->next->next;
            continue;
        }

        block = block->next;
    }
}

int arwill_kernel_heap_init(
    struct arwill_memory *memory,
    uint64_t hhdm_offset,
    size_t page_count
) {
    uint64_t first_physical = 0;
    uint64_t previous_physical = 0;

    if (memory == 0 || hhdm_offset == 0U || page_count == 0U) {
        return 0;
    }

    if (memory->kernel_heap.initialized) {
        return 1;
    }

    if (page_count > SIZE_MAX / ARWILL_MEMORY_PAGE_SIZE) {
        return 0;
    }

    for (size_t index = 0; index < page_count; index++) {
        uint64_t physical = 0;

        if (!arwill_physical_allocate_page(memory, &physical)) {
            return 0;
        }

        if (index == 0U) {
            first_physical = physical;
        } else if (physical != previous_physical + ARWILL_MEMORY_PAGE_SIZE) {
            return 0;
        }

        previous_physical = physical;
    }

    const size_t heap_size = page_count * ARWILL_MEMORY_PAGE_SIZE;
    struct heap_block *first =
        (struct heap_block *)(uintptr_t)(hhdm_offset + first_physical);

    first->size = heap_size - heap_payload_offset();
    first->free = 1;
    first->next = 0;

    memory->kernel_heap.base = first;
    memory->kernel_heap.size_bytes = heap_size;
    memory->kernel_heap.used_bytes = 0;
    memory->kernel_heap.allocation_count = 0;
    memory->kernel_heap.free_count = 0;
    memory->kernel_heap.failed_allocation_count = 0;
    memory->kernel_heap.initialized = 1;
    return 1;
}

void *arwill_kmalloc(struct arwill_memory *memory, size_t size) {
    if (memory == 0 || !memory->kernel_heap.initialized || size == 0U) {
        return 0;
    }

    const size_t aligned_size = align_size_up(size, heap_alignment);

    if (aligned_size == SIZE_MAX) {
        memory->kernel_heap.failed_allocation_count++;
        return 0;
    }

    struct heap_block *block = heap_first_block(memory);

    while (block != 0) {
        if (!block->free || block->size < aligned_size) {
            block = block->next;
            continue;
        }

        const size_t header_size = heap_payload_offset();

        if (block->size >= aligned_size + header_size + heap_alignment) {
            struct heap_block *next =
                (struct heap_block *)((uint8_t *)block + header_size + aligned_size);

            next->size = block->size - aligned_size - header_size;
            next->free = 1;
            next->next = block->next;
            block->size = aligned_size;
            block->next = next;
        }

        block->free = 0;
        memory->kernel_heap.used_bytes += block->size;
        memory->kernel_heap.allocation_count++;
        return (uint8_t *)block + header_size;
    }

    memory->kernel_heap.failed_allocation_count++;
    return 0;
}

void arwill_kfree(struct arwill_memory *memory, void *pointer) {
    if (memory == 0 || !memory->kernel_heap.initialized || pointer == 0) {
        return;
    }

    struct heap_block *block =
        (struct heap_block *)((uint8_t *)pointer - heap_payload_offset());

    if (block->free) {
        return;
    }

    block->free = 1;
    if (memory->kernel_heap.used_bytes >= block->size) {
        memory->kernel_heap.used_bytes -= block->size;
    } else {
        memory->kernel_heap.used_bytes = 0;
    }

    memory->kernel_heap.free_count++;
    heap_coalesce(memory);
}

void arwill_kernel_heap_stats(
    const struct arwill_memory *memory,
    struct arwill_kernel_heap_stats *stats
) {
    if (stats == 0) {
        return;
    }

    stats->size_bytes = 0;
    stats->used_bytes = 0;
    stats->free_bytes = 0;
    stats->allocation_count = 0;
    stats->free_count = 0;
    stats->failed_allocation_count = 0;
    stats->initialized = 0;

    if (memory == 0 || !memory->kernel_heap.initialized) {
        return;
    }

    stats->size_bytes = memory->kernel_heap.size_bytes;
    stats->used_bytes = memory->kernel_heap.used_bytes;
    stats->free_bytes = memory->kernel_heap.size_bytes - memory->kernel_heap.used_bytes;
    stats->allocation_count = memory->kernel_heap.allocation_count;
    stats->free_count = memory->kernel_heap.free_count;
    stats->failed_allocation_count = memory->kernel_heap.failed_allocation_count;
    stats->initialized = memory->kernel_heap.initialized;
}
