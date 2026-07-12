#ifndef ARWILL_ARCH_X86_64_FRAMEBUFFER_CONSOLE_H
#define ARWILL_ARCH_X86_64_FRAMEBUFFER_CONSOLE_H

#include <limine.h>

#include <arwill/kernel/console.h>

const struct arwill_console *arwill_x86_64_framebuffer_console_init(
    const struct limine_framebuffer_response *response,
    const struct arwill_console *serial_console
);

int arwill_x86_64_framebuffer_console_available(void);

const char *arwill_x86_64_framebuffer_console_status(void);

#endif
