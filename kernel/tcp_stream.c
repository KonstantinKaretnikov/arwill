#include <arwill/kernel/tcp_stream.h>

void arwill_tcp_stream_init(struct arwill_tcp_stream *stream) {
    if (stream == 0) {
        return;
    }
    arwill_tcp_listener_init(&stream->listener, 0U, 0U);
    stream->receive_head = 0;
    stream->receive_count = 0;
    stream->transmit_head = 0;
    stream->transmit_count = 0;
    stream->listening = 0;
    stream->peer_closed = 0;
    stream->close_requested = 0;
    stream->receive_window_changed = 0;
    stream->connections = 0;
    stream->disconnects = 0;
    stream->bytes_received = 0;
    stream->bytes_sent = 0;
    stream->bytes_dropped = 0;
    stream->send_failures = 0;
}

int arwill_tcp_stream_listen(struct arwill_tcp_stream *stream,
    uint16_t port, uint32_t initial_sequence) {
    if (stream == 0 || port == 0U) {
        return 0;
    }
    arwill_tcp_listener_init(&stream->listener, port, initial_sequence);
    stream->receive_head = 0;
    stream->receive_count = 0;
    stream->transmit_head = 0;
    stream->transmit_count = 0;
    stream->peer_closed = 0;
    stream->close_requested = 0;
    stream->receive_window_changed = 0;
    stream->listening = 1;
    return 1;
}

void arwill_tcp_stream_stop(struct arwill_tcp_stream *stream) {
    if (stream == 0) {
        return;
    }
    stream->bytes_dropped += (uint32_t)stream->transmit_count;
    stream->receive_head = 0;
    stream->receive_count = 0;
    stream->transmit_head = 0;
    stream->transmit_count = 0;
    stream->peer_closed = 0;
    stream->close_requested = 0;
    stream->receive_window_changed = 0;
    stream->listening = 0;
    arwill_tcp_listener_reset(&stream->listener, 0U);
}

int arwill_tcp_stream_connected(const struct arwill_tcp_stream *stream) {
    return stream != 0 && stream->listening &&
        arwill_tcp_listener_connected(&stream->listener);
}

int arwill_tcp_stream_connection_seen(const struct arwill_tcp_stream *stream) {
    return stream != 0 && stream->connections != 0U;
}

int arwill_tcp_stream_peer_closed(const struct arwill_tcp_stream *stream) {
    return stream != 0 && stream->peer_closed;
}

int arwill_tcp_stream_close_requested(const struct arwill_tcp_stream *stream) {
    return stream != 0 && stream->close_requested;
}

int arwill_tcp_stream_close_complete(const struct arwill_tcp_stream *stream) {
    return stream != 0 && stream->listening &&
        stream->listener.state == arwill_tcp_state_listen &&
        !stream->close_requested;
}

size_t arwill_tcp_stream_read(struct arwill_tcp_stream *stream,
    uint8_t *data, size_t capacity) {
    if (stream == 0 || data == 0) {
        return 0;
    }
    size_t read = 0;
    while (read < capacity && stream->receive_count != 0U) {
        data[read++] = stream->receive[stream->receive_head];
        stream->receive_head = (stream->receive_head + 1U) %
            arwill_tcp_stream_receive_capacity;
        stream->receive_count--;
    }
    if (read != 0U) {
        stream->receive_window_changed = 1;
    }
    return read;
}

size_t arwill_tcp_stream_write(struct arwill_tcp_stream *stream,
    const uint8_t *data, size_t length) {
    if (!arwill_tcp_stream_connected(stream) ||
        (data == 0 && length != 0U) ||
        length > arwill_tcp_stream_transmit_capacity - stream->transmit_count) {
        if (stream != 0 && length <= UINT32_MAX) {
            stream->bytes_dropped += (uint32_t)length;
        }
        return 0;
    }
    for (size_t index = 0; index < length; index++) {
        const size_t tail = (stream->transmit_head + stream->transmit_count) %
            arwill_tcp_stream_transmit_capacity;
        stream->transmit[tail] = data[index];
        stream->transmit_count++;
    }
    return length;
}

void arwill_tcp_stream_close(struct arwill_tcp_stream *stream) {
    if (arwill_tcp_stream_connected(stream)) {
        stream->close_requested = 1;
    }
}

const uint8_t *arwill_tcp_stream_peer_address(
    const struct arwill_tcp_stream *stream) {
    return stream == 0 ? 0 : stream->listener.peer_address;
}

uint16_t arwill_tcp_stream_peer_port(const struct arwill_tcp_stream *stream) {
    return stream == 0 ? 0U : stream->listener.peer_port;
}
