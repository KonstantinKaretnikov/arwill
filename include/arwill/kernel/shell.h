#ifndef ARWILL_KERNEL_SHELL_H
#define ARWILL_KERNEL_SHELL_H

#include <arwill/kernel/console.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>

void arwill_shell_run(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem
) __attribute__((noreturn));

#endif
