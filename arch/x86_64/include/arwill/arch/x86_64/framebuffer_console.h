#ifndef ARWILL_ARCH_X86_64_FRAMEBUFFER_CONSOLE_H
#define ARWILL_ARCH_X86_64_FRAMEBUFFER_CONSOLE_H

struct arwill_console;
struct limine_framebuffer_response;

const struct arwill_console *arwill_x86_64_framebuffer_console_init(
    const struct limine_framebuffer_response *response,
    const struct arwill_console *serial_console
);

const char *arwill_x86_64_framebuffer_console_status(void);

#endif
