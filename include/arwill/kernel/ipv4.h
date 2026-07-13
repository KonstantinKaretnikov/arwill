#ifndef ARWILL_KERNEL_IPV4_H
#define ARWILL_KERNEL_IPV4_H

#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/network.h>
#include <arwill/kernel/tcp.h>

struct arwill_clock;
struct arwill_console;

enum {
    arwill_remote_console_port = 2323,
    arwill_remote_console_receive_capacity = 512,
    arwill_tcp_pending_payload_capacity = 1024,
    arwill_tcp_retransmission_interval_ms = 250,
    arwill_tcp_max_retransmissions = 3,
};

struct arwill_tcp_pending_segment {
    struct arwill_tcp_segment segment;
    uint8_t payload[arwill_tcp_pending_payload_capacity];
    size_t payload_length;
    uint32_t expected_acknowledgement;
    uint64_t sent_at_milliseconds;
    unsigned retransmissions;
    int active;
};

struct arwill_ipv4_stack {
    const struct arwill_network_device *network;
    const struct arwill_clock *clock;
    uint8_t mac[arwill_network_mac_length];
    uint8_t address[4];
    uint8_t gateway[4];
    uint8_t gateway_mac[arwill_network_mac_length];
    int gateway_resolved;
    uint16_t echo_identifier;
    uint16_t echo_sequence;
    struct arwill_tcp_listener tcp_listener;
    uint8_t tcp_peer_mac[arwill_network_mac_length];
    uint8_t tcp_peer_address[4];
    struct arwill_tcp_pending_segment tcp_pending;
    uint8_t remote_console_receive[arwill_remote_console_receive_capacity];
    size_t remote_console_receive_head;
    size_t remote_console_receive_count;
    int remote_console_peer_closed;
    uint32_t tcp_frames_received;
    uint32_t tcp_syn_ack_sent;
    uint32_t tcp_checksum_drops;
    uint32_t tcp_duplicate_acks;
    uint32_t tcp_retransmissions;
    uint32_t tcp_timeouts;
    uint32_t remote_console_connections;
    uint32_t remote_console_disconnects;
    uint32_t remote_console_bytes_received;
    uint32_t remote_console_bytes_sent;
    uint32_t remote_console_bytes_dropped;
    uint32_t remote_console_send_failures;
};

int arwill_ipv4_init(struct arwill_ipv4_stack *stack,
    const struct arwill_network_device *network,
    const struct arwill_clock *clock);

int arwill_ipv4_send_arp_request(const struct arwill_ipv4_stack *stack,
    const uint8_t target[4]);

int arwill_ipv4_ping_gateway(struct arwill_ipv4_stack *stack);

int arwill_ipv4_service_tcp(struct arwill_ipv4_stack *stack, size_t *frames_processed);
int arwill_ipv4_poll_tcp(struct arwill_ipv4_stack *stack);

int arwill_ipv4_remote_console_connected(const struct arwill_ipv4_stack *stack);
int arwill_ipv4_remote_console_peer_closed(const struct arwill_ipv4_stack *stack);
int arwill_ipv4_remote_console_read_byte(struct arwill_ipv4_stack *stack, uint8_t *byte);
int arwill_ipv4_remote_console_write(struct arwill_ipv4_stack *stack,
    const uint8_t *data, size_t length);
void arwill_ipv4_remote_console_close(struct arwill_ipv4_stack *stack);

void arwill_ipv4_print_config(const struct arwill_ipv4_stack *stack,
    const struct arwill_console *console);

#endif
