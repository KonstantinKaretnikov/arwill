#ifndef ARWILL_KERNEL_SHELL_H
#define ARWILL_KERNEL_SHELL_H

struct arwill_block_device;
struct arwill_clock;
struct arwill_config;
struct arwill_console;
struct arwill_device_registry;
struct arwill_event_log;
struct arwill_filesystem;
struct arwill_input;
struct arwill_interrupts;
struct arwill_ipv4_stack;
struct arwill_memory;
struct arwill_network_device;
struct arwill_pci_bus;
struct arwill_power;
struct arwill_process_manager;
struct arwill_service_manager;
struct arwill_user_runtime;

void arwill_shell_run(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    struct arwill_memory *memory,
    const struct arwill_power *power,
    struct arwill_process_manager *processes,
    const struct arwill_pci_bus *pci,
    const struct arwill_network_device *network,
    struct arwill_ipv4_stack *ipv4,
    const struct arwill_block_device *block_device,
    const struct arwill_interrupts *interrupts,
    const struct arwill_clock *clock,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_device_registry *devices,
    struct arwill_config *config,
    struct arwill_event_log *log,
    struct arwill_service_manager *services
) __attribute__((noreturn));

#endif
