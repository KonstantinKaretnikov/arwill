#ifndef ARWILL_KERNEL_CONSOLE_H
#define ARWILL_KERNEL_CONSOLE_H

struct arwill_console {
    void *context;
    void (*write)(void *context, const char *text);
};

void arwill_console_write(const struct arwill_console *console, const char *text);
void arwill_console_write_line(const struct arwill_console *console, const char *text);

#endif
