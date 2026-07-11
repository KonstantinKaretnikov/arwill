#ifndef ARWILL_KERNEL_KERNEL_H
#define ARWILL_KERNEL_KERNEL_H

#include <arwill/kernel/console.h>

void arwill_kernel_start(const struct arwill_console *console) __attribute__((noreturn));

#endif
