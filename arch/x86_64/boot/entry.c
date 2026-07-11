#include <arwill/kernel/cpu.h>
#include <arwill/kernel/kernel.h>
#include <arwill/platform/qemu/serial_console.h>

void arwill_limine_entry(void) __attribute__((noreturn));

void arwill_limine_entry(void) {
    const struct arwill_console *console = arwill_qemu_serial_console_init();
    arwill_kernel_start(console);
    arwill_cpu_idle_forever();
}
