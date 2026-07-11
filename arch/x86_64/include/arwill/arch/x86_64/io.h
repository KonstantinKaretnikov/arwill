#ifndef ARWILL_ARCH_X86_64_IO_H
#define ARWILL_ARCH_X86_64_IO_H

#include <stdint.h>

static inline void arwill_x86_64_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t arwill_x86_64_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

#endif
