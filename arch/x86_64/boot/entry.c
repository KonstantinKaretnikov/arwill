#include <arwill/kernel/boot_catalog.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/kernel.h>
#include <arwill/platform/qemu/serial_console.h>

void arwill_limine_entry(void) __attribute__((noreturn));

void arwill_limine_entry(void) {
    const struct arwill_console *console = arwill_qemu_serial_console_init();
    const struct arwill_input *input = arwill_qemu_serial_input();
    const struct arwill_filesystem *filesystem = arwill_boot_catalog_filesystem();

    arwill_kernel_start(console, input, filesystem);
    arwill_cpu_idle_forever();
}
