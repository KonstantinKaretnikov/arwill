#ifndef ARWILL_KERNEL_KERNEL_H
#define ARWILL_KERNEL_KERNEL_H

#include <arwill/kernel/console.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/memory.h>

void arwill_kernel_start(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    const struct arwill_memory *memory
) __attribute__((noreturn));

#endif
