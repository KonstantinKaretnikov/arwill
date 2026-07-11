#ifndef ARWILL_KERNEL_POWER_H
#define ARWILL_KERNEL_POWER_H

struct arwill_power {
    void *context;
    void (*poweroff)(void *context);
};

void arwill_poweroff(const struct arwill_power *power) __attribute__((noreturn));

#endif
