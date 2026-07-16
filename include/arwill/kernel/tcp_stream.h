#ifndef ARWILL_KERNEL_TCP_STREAM_H
#define ARWILL_KERNEL_TCP_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/tcp.h>

enum {
    arwill_tcp_stream_receive_capacity = 512,
    arwill_tcp_stream_transmit_capacity = 8192,
};

struct arwill_tcp_stream {
    struct arwill_tcp_listener listener;
    uint8_t receive[arwill_tcp_stream_receive_capacity];
    size_t receive_head;
    size_t receive_count;
    uint8_t transmit[arwill_tcp_stream_transmit_capacity];
    size_t transmit_head;
    size_t transmit_count;
    int listening;
    int peer_closed;
    int close_requested;
    int receive_window_changed;
    uint32_t connections;
    uint32_t disconnects;
    uint32_t bytes_received;
    uint32_t bytes_sent;
    uint32_t bytes_dropped;
    uint32_t send_failures;
};

void arwill_tcp_stream_init(struct arwill_tcp_stream *stream);
int arwill_tcp_stream_listen(struct arwill_tcp_stream *stream,
    uint16_t port, uint32_t initial_sequence);
void arwill_tcp_stream_stop(struct arwill_tcp_stream *stream);
int arwill_tcp_stream_connected(const struct arwill_tcp_stream *stream);
int arwill_tcp_stream_peer_closed(const struct arwill_tcp_stream *stream);
int arwill_tcp_stream_close_requested(const struct arwill_tcp_stream *stream);
size_t arwill_tcp_stream_read(struct arwill_tcp_stream *stream,
    uint8_t *data, size_t capacity);
size_t arwill_tcp_stream_write(struct arwill_tcp_stream *stream,
    const uint8_t *data, size_t length);
void arwill_tcp_stream_close(struct arwill_tcp_stream *stream);
const uint8_t *arwill_tcp_stream_peer_address(
    const struct arwill_tcp_stream *stream);
uint16_t arwill_tcp_stream_peer_port(const struct arwill_tcp_stream *stream);

#endif
