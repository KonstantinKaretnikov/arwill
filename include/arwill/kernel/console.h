#ifndef ARWILL_KERNEL_CONSOLE_H
#define ARWILL_KERNEL_CONSOLE_H

struct arwill_console {
    void *context;
    void (*write)(void *context, const char *text);
    void (*show_boot_banner)(
        void *context,
        const char *system_name,
        const char *system_version
    );
};

void arwill_console_write(const struct arwill_console *console, const char *text);
void arwill_console_write_line(const struct arwill_console *console, const char *text);
void arwill_console_write_boot_banner(
    const struct arwill_console *console,
    const char *system_name,
    const char *system_version
);
void arwill_console_show_boot_banner(
    const struct arwill_console *console,
    const char *system_name,
    const char *system_version
);

#endif
