#ifndef ARWILL_PLATFORM_QEMU_SERIAL_CONSOLE_H
#define ARWILL_PLATFORM_QEMU_SERIAL_CONSOLE_H

#include <arwill/kernel/console.h>
#include <arwill/kernel/input.h>

const struct arwill_console *arwill_qemu_serial_console_init(void);
const struct arwill_input *arwill_qemu_serial_input(void);

#endif
