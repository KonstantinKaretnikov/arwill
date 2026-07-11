#include <arwill/identity.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/kernel.h>

void arwill_kernel_start(const struct arwill_console *console) {
    arwill_console_write(console, ARWILL_PROJECT_NAME);
    arwill_console_write(console, " ");
    arwill_console_write_line(console, ARWILL_PROJECT_VERSION);
    arwill_console_write_line(console, "architecture: " ARWILL_TARGET_ARCHITECTURE);
    arwill_console_write_line(console, "platform: " ARWILL_TARGET_PLATFORM);
    arwill_console_write_line(console, "console: serial");
    arwill_console_write_line(console, "status: kernel initialized");

    arwill_cpu_idle_forever();
}
