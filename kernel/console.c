#include <arwill/kernel/console.h>

void arwill_console_write(const struct arwill_console *console, const char *text) {
    if (console == 0 || console->write == 0 || text == 0) {
        return;
    }

    console->write(console->context, text);
}

void arwill_console_write_line(const struct arwill_console *console, const char *text) {
    arwill_console_write(console, text);
    arwill_console_write(console, "\n");
}

void arwill_console_write_boot_banner(
    const struct arwill_console *console,
    const char *system_name,
    const char *system_version
) {
    if (console == 0 || system_name == 0 || system_version == 0) {
        return;
    }

    arwill_console_write_line(console, "             /\\");
    arwill_console_write_line(console, "            /  \\");
    arwill_console_write_line(console, "           / /\\ \\       ARWILL");
    arwill_console_write_line(console, "          / ____ \\      ARCHITECTURE IS THE PRODUCT");
    arwill_console_write_line(console, "         /_/    \\_\\");
    arwill_console_write_line(console, "");
    arwill_console_write(console, system_name);
    arwill_console_write(console, " ");
    arwill_console_write(console, system_version);
    arwill_console_write_line(console, " ready");
}

void arwill_console_show_boot_banner(
    const struct arwill_console *console,
    const char *system_name,
    const char *system_version
) {
    if (console == 0 || system_name == 0 || system_version == 0) {
        return;
    }

    if (console->show_boot_banner != 0) {
        console->show_boot_banner(console->context, system_name, system_version);
        return;
    }

    arwill_console_write_boot_banner(console, system_name, system_version);
}
