#ifndef ARWILL_KERNEL_SHELL_H
#define ARWILL_KERNEL_SHELL_H

#include <arwill/kernel/block_device.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/config.h>
#include <arwill/kernel/device.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/pci.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/log.h>
#include <arwill/kernel/user.h>

void arwill_shell_run(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    struct arwill_memory *memory,
    const struct arwill_power *power,
    struct arwill_process_manager *processes,
    const struct arwill_pci_bus *pci,
    const struct arwill_network_device *network,
    struct arwill_ipv4_stack *ipv4,
    const struct arwill_block_device *block_device,
    const struct arwill_interrupts *interrupts,
    const struct arwill_clock *clock,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_device_registry *devices,
    struct arwill_config *config,
    struct arwill_event_log *log
) __attribute__((noreturn));

#endif
