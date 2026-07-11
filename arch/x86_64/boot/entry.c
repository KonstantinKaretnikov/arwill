#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include <arwill/arch/x86_64/limine_requests.h>
#include <arwill/kernel/boot_catalog.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/kernel.h>
#include <arwill/kernel/memory.h>
#include <arwill/platform/qemu/ata_pio.h>
#include <arwill/platform/qemu/power.h>
#include <arwill/platform/qemu/serial_console.h>

void arwill_limine_entry(void) __attribute__((noreturn));

enum {
    limine_memory_region_capacity = 64,
};

static struct arwill_memory_region arwill_limine_memory_regions[limine_memory_region_capacity];
static struct arwill_memory arwill_limine_memory;

static enum arwill_memory_region_type convert_limine_memory_region_type(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
            return arwill_memory_region_usable;
        case LIMINE_MEMMAP_RESERVED:
            return arwill_memory_region_reserved;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            return arwill_memory_region_acpi_reclaimable;
        case LIMINE_MEMMAP_ACPI_NVS:
            return arwill_memory_region_acpi_nvs;
        case LIMINE_MEMMAP_BAD_MEMORY:
            return arwill_memory_region_bad_memory;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return arwill_memory_region_bootloader_reclaimable;
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
            return arwill_memory_region_executable_and_modules;
        case LIMINE_MEMMAP_FRAMEBUFFER:
            return arwill_memory_region_framebuffer;
        case LIMINE_MEMMAP_RESERVED_MAPPED:
            return arwill_memory_region_reserved_mapped;
    }

    return arwill_memory_region_unknown;
}

static void initialize_memory_from_limine(void) {
    const struct limine_memmap_response *response = arwill_limine_memmap_response();
    size_t region_count = 0;
    int truncated = 0;

    if (response != 0 && response->entries != 0) {
        for (size_t index = 0; (uint64_t)index < response->entry_count; index++) {
            const struct limine_memmap_entry *entry = response->entries[index];

            if (entry == 0) {
                continue;
            }

            if (region_count >= limine_memory_region_capacity) {
                truncated = 1;
                continue;
            }

            arwill_limine_memory_regions[region_count].base = entry->base;
            arwill_limine_memory_regions[region_count].length = entry->length;
            arwill_limine_memory_regions[region_count].type =
                convert_limine_memory_region_type(entry->type);
            region_count++;
        }
    }

    arwill_memory_init(
        &arwill_limine_memory,
        arwill_limine_memory_regions,
        region_count,
        truncated
    );
}

void arwill_limine_entry(void) {
    initialize_memory_from_limine();

    const struct arwill_console *console = arwill_qemu_serial_console_init();
    const struct arwill_input *input = arwill_qemu_serial_input();
    const struct arwill_filesystem *filesystem = arwill_boot_catalog_filesystem();
    const struct arwill_power *power = arwill_qemu_power();
    const struct arwill_block_device *block_device = arwill_qemu_ata_block_device_init();

    arwill_kernel_start(console, input, filesystem, &arwill_limine_memory, power, block_device);
    arwill_cpu_idle_forever();
}
