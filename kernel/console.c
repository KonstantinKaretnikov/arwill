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
