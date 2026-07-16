#ifndef ARWILL_KERNEL_LOG_H
#define ARWILL_KERNEL_LOG_H

#include <stddef.h>
#include <stdint.h>

struct arwill_clock;

enum {
    arwill_event_log_capacity = 64
};

enum arwill_log_severity {
    arwill_log_info,
    arwill_log_warning,
    arwill_log_error
};

enum arwill_log_subsystem {
    arwill_log_system,
    arwill_log_config,
    arwill_log_service,
    arwill_log_network,
    arwill_log_auth,
    arwill_log_awp,
    arwill_log_filesystem
};

enum arwill_log_code {
    arwill_log_boot,
    arwill_log_config_loaded,
    arwill_log_config_changed,
    arwill_log_config_error,
    arwill_log_service_started,
    arwill_log_service_stopped,
    arwill_log_service_restarted,
    arwill_log_tcp_connected,
    arwill_log_tcp_disconnected,
    arwill_log_tcp_timeout,
    arwill_log_auth_accepted,
    arwill_log_auth_rejected,
    arwill_log_awp_started,
    arwill_log_awp_exited,
    arwill_log_awp_faulted,
    arwill_log_file_written
};

struct arwill_log_entry {
    uint64_t milliseconds;
    enum arwill_log_severity severity;
    enum arwill_log_subsystem subsystem;
    enum arwill_log_code code;
    uint64_t argument0;
    uint64_t argument1;
};

struct arwill_event_log {
    const struct arwill_clock *clock;
    struct arwill_log_entry entries[arwill_event_log_capacity];
    size_t head;
    size_t count;
    uint64_t overwritten;
};

void arwill_event_log_init(
    struct arwill_event_log *log,
    const struct arwill_clock *clock
);

void arwill_event_log_record(
    struct arwill_event_log *log,
    enum arwill_log_severity severity,
    enum arwill_log_subsystem subsystem,
    enum arwill_log_code code,
    uint64_t argument0,
    uint64_t argument1
);

int arwill_event_log_entry(
    const struct arwill_event_log *log,
    size_t index,
    struct arwill_log_entry *entry
);

const char *arwill_log_severity_name(enum arwill_log_severity severity);
const char *arwill_log_subsystem_name(enum arwill_log_subsystem subsystem);
const char *arwill_log_code_name(enum arwill_log_code code);

#endif
