#include <arwill/identity.h>
#include <arwill/kernel/block_device.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/kernel.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/shell.h>

void arwill_kernel_start(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    const struct arwill_memory *memory,
    const struct arwill_power *power,
    const struct arwill_block_device *block_device
) {
    static struct arwill_process_manager process_manager;

    arwill_process_manager_init(&process_manager);

    arwill_console_write(console, ARWILL_PROJECT_NAME);
    arwill_console_write(console, " ");
    arwill_console_write_line(console, ARWILL_PROJECT_VERSION);
    arwill_console_write_line(console, "architecture: " ARWILL_TARGET_ARCHITECTURE);
    arwill_console_write_line(console, "platform: " ARWILL_TARGET_PLATFORM);
    arwill_console_write_line(console, "console: serial");
    arwill_console_write_line(console, "input: serial");
    arwill_console_write_line(console, "shell: ready");
    arwill_console_write_line(console, "filesystem: static boot catalog");
    arwill_console_write(console, "block: ");
    if (block_device == 0 || block_device->name == 0) {
        arwill_console_write_line(console, "unavailable");
    } else {
        arwill_console_write_line(console, block_device->name);
    }
    arwill_console_write_line(console, "memory: boot memory map");
    arwill_console_write_line(console, "allocator: physical page bump allocator");
    arwill_console_write_line(console, "processes: kernel cooperative");
    arwill_console_write_line(console, "power: qemu debug exit");
    arwill_console_write_line(console, "status: kernel initialized");

    arwill_shell_run(console, input, filesystem, memory, power, &process_manager, block_device);
}
