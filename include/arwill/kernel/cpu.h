#ifndef ARWILL_KERNEL_CPU_H
#define ARWILL_KERNEL_CPU_H

void arwill_cpu_idle_forever(void) __attribute__((noreturn));
void arwill_cpu_wait_for_interrupt(void);

#endif
