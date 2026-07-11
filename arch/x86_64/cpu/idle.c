#include <arwill/kernel/cpu.h>

void arwill_cpu_idle_forever(void) {
    __asm__ volatile("cli" : : : "memory");

    for (;;) {
        __asm__ volatile("hlt" : : : "memory");
    }
}
