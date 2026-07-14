#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include <arwill/arch/x86_64/framebuffer_console.h>
#include <arwill/arch/x86_64/process_context.h>
#include <arwill/arch/x86_64/interrupts.h>
#include <arwill/arch/x86_64/pci.h>
#include <arwill/arch/x86_64/limine_requests.h>
#include <arwill/arch/x86_64/user_mode.h>
#include <arwill/kernel/arfs.h>
#include <arwill/kernel/boot_catalog.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/config.h>
#include <arwill/kernel/kernel.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/log.h>
#include <arwill/kernel/pci.h>
#include <arwill/kernel/service.h>
#include <arwill/platform/qemu/ata_pio.h>
#include <arwill/platform/qemu/e1000.h>
#include <arwill/platform/qemu/power.h>
#include <arwill/platform/qemu/serial_console.h>

void arwill_limine_entry(void) __attribute__((noreturn));

enum {
    limine_memory_region_capacity = 64,
};

static struct arwill_memory_region arwill_limine_memory_regions[limine_memory_region_capacity];
static struct arwill_memory arwill_limine_memory;
static struct arwill_device_registry arwill_limine_devices;
static struct arwill_pci_bus arwill_limine_pci;
static struct arwill_ipv4_stack arwill_limine_ipv4;
static struct arwill_config arwill_limine_config;
static struct arwill_event_log arwill_limine_log;
static struct arwill_service_manager arwill_limine_services;

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
    arwill_device_registry_init(&arwill_limine_devices);
    arwill_x86_64_pci_scan(&arwill_limine_pci);

    const struct arwill_console *serial_console = arwill_qemu_serial_console_init();
    const struct arwill_console *console = arwill_x86_64_framebuffer_console_init(
        arwill_limine_framebuffer_response(),
        serial_console
    );
    const struct arwill_input *input = arwill_qemu_serial_input();
    const struct arwill_power *power = arwill_qemu_power();
    const struct arwill_block_device *block_device = arwill_qemu_ata_block_device_init();
    const struct arwill_filesystem *filesystem = arwill_arfs_mount(block_device);
    const struct limine_hhdm_response *hhdm = arwill_limine_hhdm_response();
    const uint64_t hhdm_offset = hhdm == 0 ? 0 : hhdm->offset;
    const struct arwill_clock *clock = arwill_x86_64_pit_clock();
    arwill_event_log_init(&arwill_limine_log, clock);
    arwill_event_log_record(
        &arwill_limine_log, arwill_log_info, arwill_log_system,
        arwill_log_boot, 0, 0
    );
    arwill_config_init(&arwill_limine_config, filesystem);
    const int config_ready = arwill_config_load(&arwill_limine_config);
    arwill_event_log_record(
        &arwill_limine_log,
        config_ready ? arwill_log_info : arwill_log_error,
        arwill_log_config,
        config_ready ? arwill_log_config_loaded : arwill_log_config_error,
        arwill_limine_config.loaded_from_file != 0,
        arwill_limine_config.remote_port
    );
    (void)arwill_kernel_heap_init(&arwill_limine_memory, hhdm_offset, 4);
    const struct arwill_network_device *network =
        arwill_qemu_e1000_init(&arwill_limine_pci, &arwill_limine_memory, hhdm_offset);
    const struct arwill_user_runtime *user_runtime =
        arwill_x86_64_user_mode_init(
            &arwill_limine_memory, hhdm_offset, input, clock, filesystem,
            &arwill_limine_log
        );
    const struct arwill_interrupts *interrupts = arwill_x86_64_interrupts_init();
    const int ipv4_ready = arwill_ipv4_init(
        &arwill_limine_ipv4,
        network,
        clock,
        arwill_limine_config.remote_port
    );
    arwill_service_manager_init(
        &arwill_limine_services,
        &arwill_limine_ipv4,
        &arwill_limine_config,
        &arwill_limine_log,
        ipv4_ready
    );

    (void)arwill_device_register(
        &arwill_limine_devices,
        "serial0",
        arwill_device_kind_console,
        "qemu serial",
        serial_console == 0 ? "unavailable" : "ready"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "fb0",
        arwill_device_kind_console,
        "limine framebuffer text",
        arwill_x86_64_framebuffer_console_status()
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "input0",
        arwill_device_kind_input,
        "qemu serial",
        input == 0 ? "unavailable" : "ready"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "disk0",
        arwill_device_kind_block,
        block_device == 0 || block_device->name == 0 ? "none" : block_device->name,
        block_device == 0 ? "unavailable" : "ready"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "fs0",
        arwill_device_kind_filesystem,
        filesystem == 0 || filesystem->name == 0 ? "boot catalog" : filesystem->name,
        filesystem == 0 ? "fallback" : "mounted"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "heap0",
        arwill_device_kind_memory,
        "hhdm free-list",
        arwill_limine_memory.kernel_heap.initialized ? "ready" : "unavailable"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "timer0",
        arwill_device_kind_interrupts,
        interrupts == 0 || interrupts->name == 0 ? "none" : interrupts->name,
        interrupts == 0 ? "unavailable" : "ready"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "power0",
        arwill_device_kind_power,
        "qemu debug exit",
        power == 0 ? "unavailable" : "ready"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "user0",
        arwill_device_kind_user_runtime,
        user_runtime == 0 || user_runtime->name == 0 ? "none" : user_runtime->name,
        user_runtime == 0 ? "unavailable" : "ready"
    );
    (void)arwill_device_register(
        &arwill_limine_devices,
        "net0",
        arwill_device_kind_network,
        network == 0 || network->name == 0 ? "none" : network->name,
        network == 0 ? "unavailable" : "ready"
    );

    if (filesystem == 0) {
        filesystem = arwill_boot_catalog_filesystem();
    }

    arwill_kernel_start(
        console,
        input,
        filesystem,
        &arwill_limine_memory,
        &arwill_limine_pci,
        network,
        &arwill_limine_ipv4,
        power,
        block_device,
        interrupts,
        clock,
        user_runtime,
        &arwill_limine_devices,
        &arwill_limine_config,
        &arwill_limine_log,
        &arwill_limine_services,
        arwill_x86_64_process_context_backend()
    );
    arwill_cpu_idle_forever();
}
