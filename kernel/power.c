#include <arwill/kernel/cpu.h>
#include <arwill/kernel/power.h>

void arwill_poweroff(const struct arwill_power *power) {
    if (power != 0 && power->poweroff != 0) {
        power->poweroff(power->context);
    }

    arwill_cpu_idle_forever();
}
