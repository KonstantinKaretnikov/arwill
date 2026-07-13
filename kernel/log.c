#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/clock.h>
#include <arwill/kernel/log.h>

void arwill_event_log_init(
    struct arwill_event_log *log,
    const struct arwill_clock *clock
) {
    if (log == 0) {
        return;
    }
    log->clock = clock;
    log->head = 0;
    log->count = 0;
    log->overwritten = 0;
}

void arwill_event_log_record(
    struct arwill_event_log *log,
    enum arwill_log_severity severity,
    enum arwill_log_subsystem subsystem,
    enum arwill_log_code code,
    uint64_t argument0,
    uint64_t argument1
) {
    if (log == 0) {
        return;
    }
    size_t index = (log->head + log->count) % arwill_event_log_capacity;
    if (log->count == arwill_event_log_capacity) {
        index = log->head;
        log->head = (log->head + 1U) % arwill_event_log_capacity;
        log->overwritten++;
    } else {
        log->count++;
    }
    log->entries[index].milliseconds =
        arwill_clock_monotonic_milliseconds(log->clock);
    log->entries[index].severity = severity;
    log->entries[index].subsystem = subsystem;
    log->entries[index].code = code;
    log->entries[index].argument0 = argument0;
    log->entries[index].argument1 = argument1;
}

int arwill_event_log_entry(
    const struct arwill_event_log *log,
    size_t index,
    struct arwill_log_entry *entry
) {
    if (log == 0 || entry == 0 || index >= log->count) {
        return 0;
    }
    *entry = log->entries[(log->head + index) % arwill_event_log_capacity];
    return 1;
}

const char *arwill_log_severity_name(enum arwill_log_severity severity) {
    switch (severity) {
        case arwill_log_info:
            return "info";
        case arwill_log_warning:
            return "warning";
        case arwill_log_error:
            return "error";
    }
    return "unknown";
}

const char *arwill_log_subsystem_name(enum arwill_log_subsystem subsystem) {
    switch (subsystem) {
        case arwill_log_system:
            return "system";
        case arwill_log_config:
            return "config";
        case arwill_log_service:
            return "service";
        case arwill_log_network:
            return "network";
        case arwill_log_auth:
            return "auth";
        case arwill_log_awp:
            return "awp";
        case arwill_log_filesystem:
            return "filesystem";
    }
    return "unknown";
}

const char *arwill_log_code_name(enum arwill_log_code code) {
    switch (code) {
        case arwill_log_boot:
            return "boot";
        case arwill_log_config_loaded:
            return "loaded";
        case arwill_log_config_changed:
            return "changed";
        case arwill_log_config_error:
            return "parse-error";
        case arwill_log_service_started:
            return "started";
        case arwill_log_service_stopped:
            return "stopped";
        case arwill_log_service_restarted:
            return "restarted";
        case arwill_log_tcp_connected:
            return "connected";
        case arwill_log_tcp_disconnected:
            return "disconnected";
        case arwill_log_tcp_timeout:
            return "timeout";
        case arwill_log_auth_accepted:
            return "accepted";
        case arwill_log_auth_rejected:
            return "rejected";
        case arwill_log_awp_started:
            return "started";
        case arwill_log_awp_exited:
            return "exited";
        case arwill_log_awp_faulted:
            return "faulted";
        case arwill_log_file_written:
            return "written";
    }
    return "unknown";
}
