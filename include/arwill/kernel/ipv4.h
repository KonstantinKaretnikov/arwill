#ifndef ARWILL_KERNEL_IPV4_H
#define ARWILL_KERNEL_IPV4_H

#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/network.h>
#include <arwill/kernel/tcp.h>
#include <arwill/kernel/tcp_stream.h>

struct arwill_clock;
struct arwill_console;

enum {
    arwill_tcp_pending_payload_capacity = 1024,
    arwill_tcp_pending_segment_capacity = 4,
    arwill_tcp_local_maximum_segment_size = 1024,
    arwill_tcp_retransmission_interval_ms = 250,
    arwill_tcp_retransmission_minimum_ms = 100,
    arwill_tcp_retransmission_maximum_ms = 2000,
    arwill_tcp_max_retransmissions = 3,
    arwill_tcp_time_wait_ms = 500,
    arwill_tcp_close_timeout_ms = 2500,
    arwill_tcp_endpoint_capacity = 4,
    arwill_tcp_arp_retry_ms = 250,
    arwill_tcp_arp_max_attempts = 3,
    arwill_udp_endpoint_capacity = 4,
    arwill_udp_payload_capacity = 512,
    arwill_udp_arp_retry_ms = 250,
    arwill_udp_arp_max_attempts = 3,
};

struct arwill_tcp_pending_segment {
    struct arwill_tcp_segment segment;
    uint8_t payload[arwill_tcp_pending_payload_capacity];
    size_t payload_length;
    uint32_t expected_acknowledgement;
    uint64_t sent_at_milliseconds;
    uint64_t retransmission_timeout_ms;
    unsigned retransmissions;
    int active;
};

struct arwill_tcp_endpoint {
    struct arwill_tcp_stream stream;
    uint8_t tcp_peer_mac[arwill_network_mac_length];
    struct arwill_tcp_pending_segment
        tcp_pending[arwill_tcp_pending_segment_capacity];
    size_t tcp_pending_head;
    size_t tcp_pending_count;
    uint64_t tcp_close_started_milliseconds;
    uint64_t tcp_time_wait_started_milliseconds;
    uint64_t tcp_smoothed_round_trip_ms;
    uint64_t tcp_round_trip_variance_ms;
    uint64_t tcp_retransmission_timeout_ms;
    uint16_t tcp_last_advertised_window;
    int tcp_window_update_pending;
    uint8_t connect_peer_address[4];
    uint8_t connect_next_hop[4];
    uint16_t connect_peer_port;
    uint16_t connect_local_port;
    uint16_t bound_port;
    uint64_t connect_arp_sent_milliseconds;
    unsigned connect_arp_attempts;
    int connect_pending;
    int connect_failed;
    int active_open;
    int allocated;
};

struct arwill_tcp_endpoint_snapshot {
    int allocated;
    int listening;
    int connected;
    const char *owner;
    const char *state;
    uint16_t local_port;
    uint8_t peer_address[4];
    uint16_t peer_port;
    size_t receive_available;
    uint16_t peer_window;
    uint16_t peer_maximum_segment_size;
    size_t pending_segments;
    uint64_t smoothed_round_trip_ms;
    uint64_t retransmission_timeout_ms;
    uint32_t connections;
    uint32_t disconnects;
    uint32_t bytes_received;
    uint32_t bytes_sent;
    uint32_t bytes_dropped;
    uint32_t send_failures;
};

struct arwill_udp_endpoint {
    uint8_t receive[arwill_udp_payload_capacity];
    size_t receive_length;
    uint8_t peer_address[4];
    uint8_t next_hop[4];
    uint8_t peer_mac[arwill_network_mac_length];
    uint16_t local_port;
    uint16_t peer_port;
    uint64_t arp_sent_milliseconds;
    unsigned arp_attempts;
    int allocated;
    int bound;
    int connected;
    int resolving;
    int failed;
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
    uint32_t icmp_echo_requests_received;
    uint32_t icmp_echo_replies_sent;
    uint32_t icmp_checksum_drops;
    struct arwill_tcp_endpoint endpoints[arwill_tcp_endpoint_capacity];
    uint32_t tcp_frames_received;
    uint32_t tcp_frames_sent;
    uint32_t tcp_bytes_received;
    uint32_t tcp_bytes_sent;
    uint32_t tcp_syn_received;
    uint32_t tcp_syn_ack_sent;
    uint32_t tcp_fin_received;
    uint32_t tcp_rst_received;
    uint32_t tcp_rst_sent;
    uint32_t tcp_unknown_port_frames;
    uint32_t tcp_tuple_mismatches;
    uint32_t tcp_checksum_drops;
    uint32_t tcp_duplicate_acks;
    uint32_t tcp_retransmissions;
    uint32_t tcp_retransmission_backoffs;
    uint32_t tcp_timeouts;
    uint32_t tcp_receive_window_drops;
    uint32_t tcp_window_updates;
    struct arwill_udp_endpoint udp_endpoints[arwill_udp_endpoint_capacity];
    uint32_t udp_frames_received;
    uint32_t udp_frames_sent;
    uint32_t udp_bytes_received;
    uint32_t udp_bytes_sent;
    uint32_t udp_checksum_drops;
    uint32_t udp_port_drops;
    uint32_t udp_queue_drops;
};

int arwill_ipv4_init(struct arwill_ipv4_stack *stack,
    const struct arwill_network_device *network,
    const struct arwill_clock *clock,
    uint16_t remote_console_port);

int arwill_ipv4_send_arp_request(const struct arwill_ipv4_stack *stack,
    const uint8_t target[4]);

int arwill_ipv4_ping_gateway(struct arwill_ipv4_stack *stack);

int arwill_ipv4_service_tcp(struct arwill_ipv4_stack *stack, size_t *frames_processed);
int arwill_ipv4_poll_tcp(struct arwill_ipv4_stack *stack);

struct arwill_tcp_stream *arwill_ipv4_tcp_open(struct arwill_ipv4_stack *stack);
int arwill_ipv4_tcp_bind(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, uint16_t port);
int arwill_ipv4_tcp_listen(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, uint16_t port);
int arwill_ipv4_tcp_connect(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, const uint8_t peer_address[4],
    uint16_t local_port, uint16_t peer_port);
int arwill_ipv4_tcp_connect_status(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream);
void arwill_ipv4_tcp_release(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream);
struct arwill_udp_endpoint *arwill_ipv4_udp_open(
    struct arwill_ipv4_stack *stack);
int arwill_ipv4_udp_bind(struct arwill_ipv4_stack *stack,
    struct arwill_udp_endpoint *endpoint, uint16_t port);
int arwill_ipv4_udp_connect(struct arwill_ipv4_stack *stack,
    struct arwill_udp_endpoint *endpoint, const uint8_t peer_address[4],
    uint16_t peer_port);
int arwill_ipv4_udp_connect_status(struct arwill_ipv4_stack *stack,
    struct arwill_udp_endpoint *endpoint);
int arwill_ipv4_udp_send(struct arwill_ipv4_stack *stack,
    struct arwill_udp_endpoint *endpoint, const uint8_t *data, size_t length);
long arwill_ipv4_udp_receive(struct arwill_ipv4_stack *stack,
    struct arwill_udp_endpoint *endpoint, uint8_t *data, size_t capacity);
void arwill_ipv4_udp_release(struct arwill_ipv4_stack *stack,
    struct arwill_udp_endpoint *endpoint);
struct arwill_tcp_stream *arwill_ipv4_remote_stream(
    struct arwill_ipv4_stack *stack);
int arwill_ipv4_tcp_endpoint_snapshot(
    const struct arwill_ipv4_stack *stack,
    size_t index,
    struct arwill_tcp_endpoint_snapshot *snapshot
);

void arwill_ipv4_print_config(const struct arwill_ipv4_stack *stack,
    const struct arwill_console *console);

#endif
