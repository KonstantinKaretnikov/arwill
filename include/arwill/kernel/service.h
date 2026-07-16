#ifndef ARWILL_KERNEL_SERVICE_H
#define ARWILL_KERNEL_SERVICE_H

struct arwill_config;
struct arwill_event_log;
struct arwill_ipv4_stack;
struct arwill_tcp_stream;

enum arwill_service_state {
    arwill_service_stopped,
    arwill_service_running,
    arwill_service_failed,
    arwill_service_unavailable
};

struct arwill_service_manager {
    struct arwill_ipv4_stack *ipv4;
    struct arwill_tcp_stream *remote_stream;
    struct arwill_config *config;
    struct arwill_event_log *log;
    enum arwill_service_state remote_console_state;
};

void arwill_service_manager_init(
    struct arwill_service_manager *manager,
    struct arwill_ipv4_stack *ipv4,
    struct arwill_tcp_stream *remote_stream,
    struct arwill_config *config,
    struct arwill_event_log *log,
    int network_ready
);

int arwill_service_remote_console_start(struct arwill_service_manager *manager);
int arwill_service_remote_console_stop(struct arwill_service_manager *manager);
int arwill_service_remote_console_restart(struct arwill_service_manager *manager);

const char *arwill_service_state_name(enum arwill_service_state state);

#endif
