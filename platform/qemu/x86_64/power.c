#include <arwill/arch/x86_64/io.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/power.h>
#include <arwill/platform/qemu/power.h>

enum {
    qemu_debug_exit_port = 0x00f4,
    qemu_debug_exit_success_value = 0x10,
};

static void qemu_poweroff(void *context) {
    (void)context;

    arwill_x86_64_out32(qemu_debug_exit_port, qemu_debug_exit_success_value);
    arwill_cpu_idle_forever();
}

static const struct arwill_power qemu_power = {
    .context = 0,
    .poweroff = qemu_poweroff,
};

const struct arwill_power *arwill_qemu_power(void) {
    return &qemu_power;
}
