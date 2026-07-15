#ifndef ARWILL_KERNEL_TCP_H
#define ARWILL_KERNEL_TCP_H

#include <stddef.h>
#include <stdint.h>

enum arwill_tcp_state {
    arwill_tcp_state_listen,
    arwill_tcp_state_syn_received,
    arwill_tcp_state_established,
    arwill_tcp_state_close_wait,
    arwill_tcp_state_last_ack,
    arwill_tcp_state_fin_wait_1,
    arwill_tcp_state_fin_wait_2,
    arwill_tcp_state_closing,
    arwill_tcp_state_time_wait
};

enum {
    arwill_tcp_flag_fin = 1U,
    arwill_tcp_flag_syn = 2U,
    arwill_tcp_flag_rst = 4U,
    arwill_tcp_flag_psh = 8U,
    arwill_tcp_flag_ack = 16U
};

struct arwill_tcp_segment {
    uint8_t source_address[4];
    uint8_t destination_address[4];
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
    uint8_t local_address[4];
    uint8_t peer_address[4];
    uint32_t sequence;
    uint32_t acknowledgement;
    enum arwill_tcp_state state;
};

void arwill_tcp_listener_init(struct arwill_tcp_listener *listener, uint16_t port,
    uint32_t initial_sequence);

void arwill_tcp_listener_reset(struct arwill_tcp_listener *listener,
    uint32_t initial_sequence);

int arwill_tcp_listener_receive(struct arwill_tcp_listener *listener,
    const struct arwill_tcp_segment *incoming, struct arwill_tcp_segment *reply);

int arwill_tcp_listener_begin_close(struct arwill_tcp_listener *listener,
    struct arwill_tcp_segment *segment);

int arwill_tcp_listener_connected(const struct arwill_tcp_listener *listener);

int arwill_tcp_listener_matches(const struct arwill_tcp_listener *listener,
    const struct arwill_tcp_segment *segment);

int arwill_tcp_sequence_before(uint32_t left, uint32_t right);
int arwill_tcp_sequence_after(uint32_t left, uint32_t right);

const char *arwill_tcp_state_name(enum arwill_tcp_state state);

#endif
