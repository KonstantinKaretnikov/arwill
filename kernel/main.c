#include <arwill/identity.h>
#include <arwill/kernel/block_device.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/config.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/kernel.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/power.h>
#include <arwill/kernel/process.h>
#include <arwill/kernel/pci.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/log.h>
#include <arwill/kernel/scheduler.h>
#include <arwill/kernel/service.h>
#include <arwill/kernel/shell.h>
#include <arwill/kernel/user.h>

void arwill_kernel_start(
    const struct arwill_console *console,
    const struct arwill_input *input,
    const struct arwill_filesystem *filesystem,
    struct arwill_memory *memory,
    const struct arwill_pci_bus *pci,
    const struct arwill_network_device *network,
    struct arwill_ipv4_stack *ipv4,
    const struct arwill_power *power,
    const struct arwill_block_device *block_device,
    const struct arwill_interrupts *interrupts,
    const struct arwill_clock *clock,
    const struct arwill_user_runtime *user_runtime,
    const struct arwill_device_registry *devices,
    struct arwill_config *config,
    struct arwill_event_log *log,
    struct arwill_service_manager *services,
    const struct arwill_process_context_backend *process_context_backend
) {
    static struct arwill_process_manager process_manager;

    arwill_process_manager_init(&process_manager, process_context_backend);
    arwill_scheduler_init();

    arwill_interrupts_enable(interrupts);

    arwill_console_show_boot_banner(
        console,
        ARWILL_PROJECT_NAME,
        ARWILL_PROJECT_VERSION
    );
    arwill_console_write_line(console, "config: /owner/arwill.conf");
    arwill_console_write_line(console, "help: type 'help' or press Tab");

    arwill_shell_run(
        console,
        input,
        filesystem,
        memory,
        power,
        &process_manager,
        pci,
        network,
        ipv4,
        block_device,
        interrupts,
        clock,
        user_runtime,
        devices,
        config,
        log,
        services
    );
}
