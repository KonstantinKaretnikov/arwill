#ifndef ARWILL_KERNEL_SHELL_H
#define ARWILL_KERNEL_SHELL_H

#include <arwill/kernel/console.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/power.h>

void arwill_shell_run(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    const struct arwill_memory *memory,
    const struct arwill_power *power
) __attribute__((noreturn));

#endif
