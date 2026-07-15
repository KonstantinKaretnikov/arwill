#include <arwill/kernel/tcp.h>

static void copy_address(uint8_t destination[4], const uint8_t source[4]) {
    for (size_t index = 0; index < 4U; index++) {
        destination[index] = source[index];
    }
}

static int same_address(const uint8_t left[4], const uint8_t right[4]) {
    for (size_t index = 0; index < 4U; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

int arwill_tcp_sequence_before(uint32_t left, uint32_t right) {
    return left != right && (uint32_t)(left - right) > 0x80000000U;
}

int arwill_tcp_sequence_after(uint32_t left, uint32_t right) {
    return arwill_tcp_sequence_before(right, left);
}

static void prepare_reply(const struct arwill_tcp_segment *incoming,
    struct arwill_tcp_segment *reply) {
    copy_address(reply->source_address, incoming->destination_address);
    copy_address(reply->destination_address, incoming->source_address);
    reply->source_port = incoming->destination_port;
    reply->destination_port = incoming->source_port;
    reply->sequence = 0;
    reply->acknowledgement = 0;
    reply->flags = 0;
    reply->window = 0U;
    reply->maximum_segment_size = 0U;
    reply->payload_length = 0;
}

void arwill_tcp_listener_init(struct arwill_tcp_listener *listener, uint16_t port,
    uint32_t initial_sequence) {
    if (listener == 0) {
        return;
    }
    listener->port = port;
    listener->peer_port = 0;
    for (size_t index = 0; index < 4U; index++) {
        listener->local_address[index] = 0U;
        listener->peer_address[index] = 0U;
    }
    listener->sequence = initial_sequence;
    listener->acknowledgement = 0;
    listener->peer_window = 0U;
    listener->peer_maximum_segment_size = 536U;
    listener->state = arwill_tcp_state_listen;
}

int arwill_tcp_listener_connected(const struct arwill_tcp_listener *listener) {
    return listener != 0 &&
        (listener->state == arwill_tcp_state_established ||
            listener->state == arwill_tcp_state_close_wait);
}

int arwill_tcp_listener_matches(const struct arwill_tcp_listener *listener,
    const struct arwill_tcp_segment *segment) {
    if (listener == 0 || segment == 0 ||
        segment->destination_port != listener->port) {
        return 0;
    }
    if (listener->state == arwill_tcp_state_listen) {
        return 1;
    }
    return segment->source_port == listener->peer_port &&
        same_address(segment->destination_address, listener->local_address) &&
        same_address(segment->source_address, listener->peer_address);
}

static int acknowledgement_covers(const struct arwill_tcp_listener *listener,
    uint32_t acknowledgement) {
    return acknowledgement == listener->sequence ||
        arwill_tcp_sequence_after(acknowledgement, listener->sequence);
}

void arwill_tcp_listener_reset(struct arwill_tcp_listener *listener,
    uint32_t initial_sequence) {
    if (listener == 0) {
        return;
    }

    arwill_tcp_listener_init(listener, listener->port, initial_sequence);
}

int arwill_tcp_listener_receive(struct arwill_tcp_listener *listener,
    const struct arwill_tcp_segment *incoming, struct arwill_tcp_segment *reply) {
    if (listener == 0 || incoming == 0 || reply == 0 ||
        !arwill_tcp_listener_matches(listener, incoming)) {
        return 0;
    }

    prepare_reply(incoming, reply);

    listener->peer_window = incoming->window;

    if ((incoming->flags & arwill_tcp_flag_rst) != 0U) {
        if (listener->state != arwill_tcp_state_listen) {
            arwill_tcp_listener_reset(listener, listener->sequence);
            return 1;
        }
        return 0;
    }

    if (listener->state == arwill_tcp_state_listen &&
        (incoming->flags & arwill_tcp_flag_syn) != 0U) {
        copy_address(listener->local_address, incoming->destination_address);
        copy_address(listener->peer_address, incoming->source_address);
        listener->peer_port = incoming->source_port;
        if (incoming->maximum_segment_size != 0U) {
            listener->peer_maximum_segment_size = incoming->maximum_segment_size;
        }
        listener->acknowledgement = incoming->sequence + 1U;
        reply->sequence = listener->sequence;
        reply->acknowledgement = listener->acknowledgement;
        reply->flags = arwill_tcp_flag_syn | arwill_tcp_flag_ack;
        listener->state = arwill_tcp_state_syn_received;
        return 1;
    }

    if (listener->state == arwill_tcp_state_syn_received &&
        incoming->source_port == listener->peer_port &&
        (incoming->flags & arwill_tcp_flag_syn) != 0U &&
        incoming->sequence + 1U == listener->acknowledgement) {
        reply->sequence = listener->sequence;
        reply->acknowledgement = listener->acknowledgement;
        reply->flags = arwill_tcp_flag_syn | arwill_tcp_flag_ack;
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

    if (listener->state == arwill_tcp_state_established
        && incoming->source_port == listener->peer_port
        && (incoming->flags & arwill_tcp_flag_ack) != 0U
        && !arwill_tcp_sequence_after(incoming->acknowledgement, listener->sequence)
        && incoming->sequence == listener->acknowledgement) {
        size_t consumed = incoming->payload_length;
        if ((incoming->flags & arwill_tcp_flag_fin) != 0U) {
            consumed++;
        }
        if (consumed > UINT32_MAX) {
            return 0;
        }
        if (consumed != 0U) {
            listener->acknowledgement += (uint32_t)consumed;
            reply->sequence = listener->sequence;
            reply->acknowledgement = listener->acknowledgement;
            reply->flags = arwill_tcp_flag_ack;
        }
        if ((incoming->flags & arwill_tcp_flag_fin) != 0U) {
            listener->state = arwill_tcp_state_close_wait;
        }
        return 1;
    }

    if (listener->state == arwill_tcp_state_established
        && incoming->source_port == listener->peer_port
        && (incoming->flags & arwill_tcp_flag_ack) != 0U
        && !arwill_tcp_sequence_after(incoming->acknowledgement, listener->sequence)
        && incoming->sequence != listener->acknowledgement) {
        reply->sequence = listener->sequence;
        reply->acknowledgement = listener->acknowledgement;
        reply->flags = arwill_tcp_flag_ack;
        return 1;
    }

    if (listener->state == arwill_tcp_state_close_wait &&
        (incoming->flags & arwill_tcp_flag_ack) != 0U &&
        !arwill_tcp_sequence_after(incoming->acknowledgement, listener->sequence)) {
        return 1;
    }

    if (listener->state == arwill_tcp_state_last_ack &&
        (incoming->flags & arwill_tcp_flag_ack) != 0U &&
        acknowledgement_covers(listener, incoming->acknowledgement)) {
        arwill_tcp_listener_reset(listener, listener->sequence);
        return 1;
    }

    if (listener->state == arwill_tcp_state_fin_wait_1) {
        const int acknowledged = (incoming->flags & arwill_tcp_flag_ack) != 0U &&
            acknowledgement_covers(listener, incoming->acknowledgement);
        const int finished = (incoming->flags & arwill_tcp_flag_fin) != 0U &&
            incoming->sequence == listener->acknowledgement;
        if (finished) {
            listener->acknowledgement++;
            reply->sequence = listener->sequence;
            reply->acknowledgement = listener->acknowledgement;
            reply->flags = arwill_tcp_flag_ack;
            listener->state = acknowledged ? arwill_tcp_state_time_wait :
                arwill_tcp_state_closing;
            return 1;
        }
        if (acknowledged) {
            listener->state = arwill_tcp_state_fin_wait_2;
            return 1;
        }
    }

    if (listener->state == arwill_tcp_state_fin_wait_2 &&
        (incoming->flags & arwill_tcp_flag_fin) != 0U &&
        incoming->sequence == listener->acknowledgement) {
        listener->acknowledgement++;
        reply->sequence = listener->sequence;
        reply->acknowledgement = listener->acknowledgement;
        reply->flags = arwill_tcp_flag_ack;
        listener->state = arwill_tcp_state_time_wait;
        return 1;
    }

    if (listener->state == arwill_tcp_state_closing &&
        (incoming->flags & arwill_tcp_flag_ack) != 0U &&
        acknowledgement_covers(listener, incoming->acknowledgement)) {
        listener->state = arwill_tcp_state_time_wait;
        return 1;
    }

    if (listener->state == arwill_tcp_state_time_wait &&
        (incoming->flags & arwill_tcp_flag_fin) != 0U) {
        reply->sequence = listener->sequence;
        reply->acknowledgement = listener->acknowledgement;
        reply->flags = arwill_tcp_flag_ack;
        return 1;
    }

    return 0;
}

int arwill_tcp_listener_begin_close(struct arwill_tcp_listener *listener,
    struct arwill_tcp_segment *segment) {
    if (listener == 0 || segment == 0 ||
        (listener->state != arwill_tcp_state_established &&
            listener->state != arwill_tcp_state_close_wait)) {
        return 0;
    }
    copy_address(segment->source_address, listener->local_address);
    copy_address(segment->destination_address, listener->peer_address);
    segment->source_port = listener->port;
    segment->destination_port = listener->peer_port;
    segment->sequence = listener->sequence;
    segment->acknowledgement = listener->acknowledgement;
    segment->flags = arwill_tcp_flag_fin | arwill_tcp_flag_ack;
    segment->window = 0U;
    segment->maximum_segment_size = 0U;
    segment->payload_length = 0;
    listener->sequence++;
    listener->state = listener->state == arwill_tcp_state_close_wait
        ? arwill_tcp_state_last_ack : arwill_tcp_state_fin_wait_1;
    return 1;
}

const char *arwill_tcp_state_name(enum arwill_tcp_state state) {
    switch (state) {
        case arwill_tcp_state_listen:
            return "listen";
        case arwill_tcp_state_syn_received:
            return "syn-received";
        case arwill_tcp_state_established:
            return "established";
        case arwill_tcp_state_close_wait:
            return "close-wait";
        case arwill_tcp_state_last_ack:
            return "last-ack";
        case arwill_tcp_state_fin_wait_1:
            return "fin-wait-1";
        case arwill_tcp_state_fin_wait_2:
            return "fin-wait-2";
        case arwill_tcp_state_closing:
            return "closing";
        case arwill_tcp_state_time_wait:
            return "time-wait";
    }
    return "unknown";
}
