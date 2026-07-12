#ifndef ARWILL_KERNEL_TCP_H
#define ARWILL_KERNEL_TCP_H

#include <stddef.h>
#include <stdint.h>

enum arwill_tcp_state {
    arwill_tcp_state_listen,
    arwill_tcp_state_syn_received,
    arwill_tcp_state_established
};

enum {
    arwill_tcp_flag_fin = 1U,
    arwill_tcp_flag_syn = 2U,
    arwill_tcp_flag_rst = 4U,
    arwill_tcp_flag_psh = 8U,
    arwill_tcp_flag_ack = 16U
};

struct arwill_tcp_segment {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t flags;
    size_t payload_length;
};

struct arwill_tcp_listener {
    uint16_t port;
    uint16_t peer_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    enum arwill_tcp_state state;
};

void arwill_tcp_listener_init(struct arwill_tcp_listener *listener, uint16_t port,
    uint32_t initial_sequence);

int arwill_tcp_listener_receive(struct arwill_tcp_listener *listener,
    const struct arwill_tcp_segment *incoming, struct arwill_tcp_segment *reply);

const char *arwill_tcp_state_name(enum arwill_tcp_state state);

#endif
