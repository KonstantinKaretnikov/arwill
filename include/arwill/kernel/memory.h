#ifndef ARWILL_KERNEL_MEMORY_H
#define ARWILL_KERNEL_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define ARWILL_MEMORY_PAGE_SIZE 4096U
#define ARWILL_PHYSICAL_ALLOCATOR_RANGE_CAPACITY 64U

enum arwill_memory_region_type {
    arwill_memory_region_usable,
    arwill_memory_region_reserved,
    arwill_memory_region_acpi_reclaimable,
    arwill_memory_region_acpi_nvs,
    arwill_memory_region_bad_memory,
    arwill_memory_region_bootloader_reclaimable,
    arwill_memory_region_executable_and_modules,
    arwill_memory_region_framebuffer,
    arwill_memory_region_reserved_mapped,
    arwill_memory_region_unknown
};

struct arwill_memory_region {
    uint64_t base;
    uint64_t length;
    enum arwill_memory_region_type type;
};

struct arwill_memory_map {
    const struct arwill_memory_region *regions;
    size_t count;
    int truncated;
};

struct arwill_physical_allocator_range {
    uint64_t next;
    uint64_t end;
};

struct arwill_physical_allocator {
    struct arwill_physical_allocator_range ranges[ARWILL_PHYSICAL_ALLOCATOR_RANGE_CAPACITY];
    size_t range_count;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t allocation_count;
};

struct arwill_physical_allocator_stats {
    uint64_t page_size;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t allocation_count;
    size_t range_count;
};

struct arwill_kernel_heap {
    void *base;
    size_t size_bytes;
    size_t used_bytes;
    size_t allocation_count;
    size_t free_count;
    size_t failed_allocation_count;
    int initialized;
};

struct arwill_kernel_heap_stats {
    size_t size_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t allocation_count;
    size_t free_count;
    size_t failed_allocation_count;
    int initialized;
};

struct arwill_memory {
    struct arwill_memory_map map;
    struct arwill_physical_allocator physical_allocator;
    struct arwill_kernel_heap kernel_heap;
};

void arwill_memory_init(
    struct arwill_memory *memory,
    const struct arwill_memory_region *regions,
    size_t region_count,
    int truncated
);

const struct arwill_memory_map *arwill_memory_map(const struct arwill_memory *memory);

const char *arwill_memory_region_type_name(enum arwill_memory_region_type type);

void arwill_physical_allocator_stats(
    const struct arwill_memory *memory,
    struct arwill_physical_allocator_stats *stats
);

int arwill_physical_allocate_page(struct arwill_memory *memory, uint64_t *physical_address);

int arwill_kernel_heap_init(
    struct arwill_memory *memory,
    uint64_t hhdm_offset,
    size_t page_count
);

void *arwill_kmalloc(struct arwill_memory *memory, size_t size);

void arwill_kfree(struct arwill_memory *memory, void *pointer);

void arwill_kernel_heap_stats(
    const struct arwill_memory *memory,
    struct arwill_kernel_heap_stats *stats
);

#endif
