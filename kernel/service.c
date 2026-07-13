#include <arwill/kernel/config.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/log.h>
#include <arwill/kernel/service.h>

static void record_service_event(
    struct arwill_service_manager *manager,
    enum arwill_log_severity severity,
    enum arwill_log_code code
) {
    arwill_event_log_record(
        manager == 0 ? 0 : manager->log,
        severity,
        arwill_log_service,
        code,
        manager == 0 || manager->config == 0 ? 0U : manager->config->remote_port,
        0U
    );
}

static int start_remote_console(
    struct arwill_service_manager *manager,
    enum arwill_log_code code
) {
    if (manager == 0 || manager->remote_console_state == arwill_service_unavailable ||
        manager->ipv4 == 0 || manager->config == 0) {
        return 0;
    }
    if (!manager->config->valid ||
        !arwill_ipv4_remote_console_start(
            manager->ipv4, manager->config->remote_port
        )) {
        manager->remote_console_state = arwill_service_failed;
        record_service_event(manager, arwill_log_error, code);
        return 0;
    }
    manager->remote_console_state = arwill_service_running;
    record_service_event(manager, arwill_log_info, code);
    return 1;
}

void arwill_service_manager_init(
    struct arwill_service_manager *manager,
    struct arwill_ipv4_stack *ipv4,
    struct arwill_config *config,
    struct arwill_event_log *log,
    int network_ready
) {
    if (manager == 0) {
        return;
    }
    manager->ipv4 = ipv4;
    manager->config = config;
    manager->log = log;
    if (!network_ready || ipv4 == 0) {
        manager->remote_console_state = arwill_service_unavailable;
        return;
    }
    manager->remote_console_state = arwill_service_stopped;
    if (config != 0 && config->valid && config->remote_enabled) {
        (void)start_remote_console(manager, arwill_log_service_started);
    } else {
        arwill_ipv4_remote_console_stop(ipv4);
        record_service_event(manager, arwill_log_info, arwill_log_service_stopped);
    }
}

int arwill_service_remote_console_start(struct arwill_service_manager *manager) {
    if (manager != 0 && manager->remote_console_state == arwill_service_running) {
        return 1;
    }
    return start_remote_console(manager, arwill_log_service_started);
}

int arwill_service_remote_console_stop(struct arwill_service_manager *manager) {
    if (manager == 0 || manager->remote_console_state == arwill_service_unavailable ||
        manager->ipv4 == 0) {
        return 0;
    }
    if (manager->remote_console_state != arwill_service_stopped) {
        arwill_ipv4_remote_console_stop(manager->ipv4);
        manager->remote_console_state = arwill_service_stopped;
        record_service_event(manager, arwill_log_info, arwill_log_service_stopped);
    }
    return 1;
}

int arwill_service_remote_console_restart(struct arwill_service_manager *manager) {
    if (manager == 0 || manager->remote_console_state == arwill_service_unavailable ||
        manager->ipv4 == 0 || manager->config == 0) {
        return 0;
    }
    arwill_ipv4_remote_console_stop(manager->ipv4);
    manager->remote_console_state = arwill_service_stopped;
    if (!arwill_config_load(manager->config)) {
        manager->remote_console_state = arwill_service_failed;
        record_service_event(manager, arwill_log_error, arwill_log_service_restarted);
        return 0;
    }
    return start_remote_console(manager, arwill_log_service_restarted);
}

const char *arwill_service_state_name(enum arwill_service_state state) {
    switch (state) {
        case arwill_service_stopped:
            return "stopped";
        case arwill_service_running:
            return "running";
        case arwill_service_failed:
            return "failed";
        case arwill_service_unavailable:
            return "unavailable";
    }
    return "unknown";
}
