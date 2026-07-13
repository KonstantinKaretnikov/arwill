#include <arwill/identity.h>
#include <arwill/kernel/block_device.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/kernel.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/pci.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/scheduler.h>
#include <arwill/kernel/shell.h>
#include <arwill/kernel/user.h>

void arwill_kernel_start(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    struct arwill_memory *memory,
    const struct arwill_pci_bus *pci,
    const struct arwill_network_device *network,
    struct arwill_ipv4_stack *ipv4,
    const struct arwill_power *power,
    const struct arwill_block_device *block_device,
    const struct arwill_interrupts *interrupts,
    const struct arwill_clock *clock,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_device_registry *devices
) {
    static struct arwill_process_manager process_manager;

    arwill_process_manager_init(&process_manager);
    arwill_scheduler_init();

    arwill_console_write(console, ARWILL_PROJECT_NAME);
    arwill_console_write(console, " ");
    arwill_console_write_line(console, ARWILL_PROJECT_VERSION);
    arwill_console_write_line(console, "architecture: " ARWILL_TARGET_ARCHITECTURE);
    arwill_console_write_line(console, "platform: " ARWILL_TARGET_PLATFORM);
    arwill_console_write_line(console, "console: serial");
    arwill_console_write_line(console, "input: serial");
    arwill_console_write_line(console, "owner: " ARWILL_OWNER_MODEL);
    arwill_console_write_line(console, "shell: ready");
    arwill_console_write(console, "filesystem: ");
    if (filesystem == 0 || filesystem->name == 0) {
        arwill_console_write_line(console, "unknown");
    } else {
        arwill_console_write_line(console, filesystem->name);
    }
    arwill_console_write(console, "block: ");
    if (block_device == 0 || block_device->name == 0) {
        arwill_console_write_line(console, "unavailable");
    } else {
        arwill_console_write_line(console, block_device->name);
    }
    arwill_console_write_line(console, "memory: boot memory map");
    arwill_console_write_line(console, "allocator: physical page bump allocator + kernel heap");
    arwill_console_write_line(console, "devices: registry");
    arwill_console_write_line(console, "processes: kernel cooperative");
    arwill_console_write(console, "interrupts: ");
    if (interrupts == 0 || interrupts->name == 0) {
        arwill_console_write_line(console, "unavailable");
    } else {
        arwill_console_write_line(console, interrupts->name);
    }
    arwill_console_write_line(console, "scheduler: AWP round-robin");
    arwill_console_write(console, "user: ");
    if (user_runtime == 0 || user_runtime->name == 0) {
        arwill_console_write_line(console, "unavailable");
    } else {
        arwill_console_write_line(console, user_runtime->name);
    }
    arwill_console_write_line(console, "power: qemu debug exit");
    arwill_interrupts_enable(interrupts);
    arwill_console_write_line(console, "status: kernel initialized");

    arwill_shell_run(
        console,
        input,
        filesystem,
        memory,
        power,
        &process_manager,
        pci,
        network,
        ipv4,
        block_device,
        interrupts,
        clock,
        user_runtime,
        devices
    );
}
