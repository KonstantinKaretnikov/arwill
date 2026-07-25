#ifndef ARWILL_KERNEL_AWP_NETWORK_H
#define ARWILL_KERNEL_AWP_NETWORK_H

#include <stddef.h>
#include <stdint.h>

struct arwill_ipv4_stack;
struct arwill_tcp_stream;
struct arwill_udp_endpoint;

enum {
    arwill_awp_network_handle_capacity = 2,
    arwill_awp_network_io_capacity = 256
};

enum arwill_awp_network_result {
    arwill_awp_network_retry = -1,
    arwill_awp_network_invalid = -2,
    arwill_awp_network_unavailable = -3,
    arwill_awp_network_closed = -4,
    arwill_awp_network_address_in_use = -5
};

enum arwill_awp_network_handle_state {
    arwill_awp_network_handle_free,
    arwill_awp_network_handle_open,
    arwill_awp_network_handle_bound,
    arwill_awp_network_handle_listening,
    arwill_awp_network_handle_connecting,
    arwill_awp_network_handle_closing
};

struct arwill_awp_network_handle {
    struct arwill_tcp_stream *stream;
    uint16_t local_port;
    enum arwill_awp_network_handle_state state;
};

struct arwill_awp_network_owner {
    struct arwill_awp_network_handle handles[arwill_awp_network_handle_capacity];
    struct arwill_udp_endpoint *udp;
    uint16_t udp_local_port;
    int udp_bound;
    int udp_connecting;
};

void arwill_awp_network_owner_init(struct arwill_awp_network_owner *owner);
void arwill_awp_network_cleanup(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner);
long arwill_awp_network_open(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner);
long arwill_awp_network_bind(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle, uint64_t port);
long arwill_awp_network_listen(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle);
long arwill_awp_network_connect(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle,
    uint32_t peer_address, uint64_t peer_port);
long arwill_awp_network_accept(struct arwill_awp_network_owner *owner,
    uint64_t handle);
long arwill_awp_network_read(struct arwill_awp_network_owner *owner,
    uint64_t handle, uint8_t *buffer, size_t capacity);
long arwill_awp_network_write(struct arwill_awp_network_owner *owner,
    uint64_t handle, const uint8_t *buffer, size_t length);
long arwill_awp_network_close(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle);
long arwill_awp_udp_open(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner);
long arwill_awp_udp_bind(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t port);
long arwill_awp_udp_connect(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint32_t peer_address,
    uint64_t peer_port);
long arwill_awp_udp_send(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, const uint8_t *buffer,
    size_t length);
long arwill_awp_udp_receive(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint8_t *buffer,
    size_t capacity);
long arwill_awp_udp_close(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner);

#endif
