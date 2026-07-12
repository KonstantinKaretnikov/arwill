#ifndef ARWILL_ARCH_X86_64_INTERRUPTS_H
#define ARWILL_ARCH_X86_64_INTERRUPTS_H

#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/clock.h>

const struct arwill_interrupts *arwill_x86_64_interrupts_init(void);
const struct arwill_clock *arwill_x86_64_pit_clock(void);

#endif
