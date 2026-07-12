#include <arwill/kernel/tcp.h>

void arwill_tcp_listener_init(struct arwill_tcp_listener *listener, uint16_t port,
    uint32_t initial_sequence) {
    if (listener == 0) {
        return;
    }
    listener->port = port;
    listener->peer_port = 0;
    listener->sequence = initial_sequence;
    listener->acknowledgement = 0;
    listener->state = arwill_tcp_state_listen;
}

int arwill_tcp_listener_receive(struct arwill_tcp_listener *listener,
    const struct arwill_tcp_segment *incoming, struct arwill_tcp_segment *reply) {
    if (listener == 0 || incoming == 0 || reply == 0 ||
        incoming->destination_port != listener->port ||
        (incoming->flags & arwill_tcp_flag_rst) != 0U) {
        return 0;
    }

    reply->source_port = listener->port;
    reply->destination_port = incoming->source_port;
    reply->sequence = 0;
    reply->acknowledgement = 0;
    reply->flags = 0;

    if (listener->state == arwill_tcp_state_listen &&
        (incoming->flags & arwill_tcp_flag_syn) != 0U) {
        listener->peer_port = incoming->source_port;
        listener->acknowledgement = incoming->sequence + 1U;
        reply->sequence = listener->sequence;
        reply->acknowledgement = listener->acknowledgement;
        reply->flags = arwill_tcp_flag_syn | arwill_tcp_flag_ack;
        listener->state = arwill_tcp_state_syn_received;
        return 1;
    }

    if (listener->state == arwill_tcp_state_syn_received &&
        incoming->source_port == listener->peer_port &&
        (incoming->flags & arwill_tcp_flag_ack) != 0U &&
        incoming->acknowledgement == listener->sequence + 1U) {
        listener->sequence++;
        listener->state = arwill_tcp_state_established;
        return 1;
    }

    return 0;
}

const char *arwill_tcp_state_name(enum arwill_tcp_state state) {
    switch (state) {
        case arwill_tcp_state_listen:
            return "listen";
        case arwill_tcp_state_syn_received:
            return "syn-received";
        case arwill_tcp_state_established:
            return "established";
    }
    return "unknown";
}
