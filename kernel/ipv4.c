#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/clock.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/ipv4.h>

static const uint8_t broadcast_mac[arwill_network_mac_length] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static void put16(uint8_t *buffer, size_t offset, uint16_t value) {
    buffer[offset] = (uint8_t)(value >> 8U);
    buffer[offset + 1U] = (uint8_t)value;
}

static uint16_t get16(const uint8_t *buffer, size_t offset) {
    return (uint16_t)(((uint16_t)buffer[offset] << 8U) | buffer[offset + 1U]);
}

static uint16_t checksum(const uint8_t *buffer, size_t length) {
    uint32_t sum = 0;
    for (size_t index = 0; index + 1U < length; index += 2U) {
        sum += get16(buffer, index);
    }
    if ((length & 1U) != 0U) {
        sum += (uint32_t)buffer[length - 1U] << 8U;
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xffffU) + (sum >> 16U);
    }
    return (uint16_t)~sum;
}

static int same_bytes(const uint8_t *left, const uint8_t *right, size_t length) {
    for (size_t index = 0; index < length; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static void put32(uint8_t *buffer, size_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)(value >> 24U);
    buffer[offset + 1U] = (uint8_t)(value >> 16U);
    buffer[offset + 2U] = (uint8_t)(value >> 8U);
    buffer[offset + 3U] = (uint8_t)value;
}

static uint32_t get32(const uint8_t *buffer, size_t offset) {
    return ((uint32_t)buffer[offset] << 24U) |
        ((uint32_t)buffer[offset + 1U] << 16U) |
        ((uint32_t)buffer[offset + 2U] << 8U) |
        (uint32_t)buffer[offset + 3U];
}

static void write_char(const struct arwill_console *console, char value) {
    char text[2] = { value, '\0' };
    arwill_console_write(console, text);
}

static void initialize_tcp_endpoint(struct arwill_tcp_endpoint *endpoint) {
    arwill_tcp_stream_init(&endpoint->stream);
    for (size_t index = 0; index < arwill_tcp_pending_segment_capacity; index++) {
        endpoint->tcp_pending[index].active = 0;
        endpoint->tcp_pending[index].payload_length = 0;
        endpoint->tcp_pending[index].expected_acknowledgement = 0;
        endpoint->tcp_pending[index].sent_at_milliseconds = 0;
        endpoint->tcp_pending[index].retransmission_timeout_ms =
            arwill_tcp_retransmission_interval_ms;
        endpoint->tcp_pending[index].retransmissions = 0;
    }
    endpoint->tcp_pending_head = 0;
    endpoint->tcp_pending_count = 0;
    endpoint->tcp_close_started_milliseconds = 0;
    endpoint->tcp_time_wait_started_milliseconds = 0;
    endpoint->tcp_smoothed_round_trip_ms = 0;
    endpoint->tcp_round_trip_variance_ms = 0;
    endpoint->tcp_retransmission_timeout_ms =
        arwill_tcp_retransmission_interval_ms;
    endpoint->tcp_last_advertised_window =
        arwill_tcp_stream_receive_capacity;
    endpoint->tcp_window_update_pending = 0;
    for (size_t index = 0; index < 4U; index++) {
        endpoint->connect_peer_address[index] = 0U;
        endpoint->connect_next_hop[index] = 0U;
    }
    endpoint->connect_peer_port = 0U;
    endpoint->connect_local_port = 0U;
    endpoint->bound_port = 0U;
    endpoint->connect_arp_sent_milliseconds = 0U;
    endpoint->connect_arp_attempts = 0U;
    endpoint->connect_pending = 0;
    endpoint->connect_failed = 0;
    endpoint->active_open = 0;
    endpoint->allocated = 0;
}

static void reset_tcp_stream(struct arwill_tcp_endpoint *endpoint);

int arwill_ipv4_init(struct arwill_ipv4_stack *stack,
    const struct arwill_network_device *network,
    const struct arwill_clock *clock,
    uint16_t remote_console_port) {
    static const uint8_t address[4] = { 10, 0, 2, 15 };
    static const uint8_t gateway[4] = { 10, 0, 2, 2 };

    if (stack == 0 || network == 0 || clock == 0 || remote_console_port == 0U ||
        !arwill_network_read_mac(network, stack->mac)) {
        return 0;
    }
    stack->network = network;
    stack->clock = clock;
    for (size_t index = 0; index < 4U; index++) {
        stack->address[index] = address[index];
        stack->gateway[index] = gateway[index];
    }
    stack->gateway_resolved = 0;
    stack->echo_identifier = 0x4152U;
    stack->echo_sequence = 0;
    stack->icmp_echo_requests_received = 0;
    stack->icmp_echo_replies_sent = 0;
    stack->icmp_checksum_drops = 0;
    for (size_t index = 0; index < arwill_tcp_endpoint_capacity; index++) {
        initialize_tcp_endpoint(&stack->endpoints[index]);
    }
    stack->endpoints[0].allocated = 1;
    stack->endpoints[0].bound_port = remote_console_port;
    (void)arwill_tcp_stream_listen(
        &stack->endpoints[0].stream, remote_console_port, 0x41520000U
    );
    stack->tcp_frames_received = 0;
    stack->tcp_frames_sent = 0;
    stack->tcp_bytes_received = 0;
    stack->tcp_bytes_sent = 0;
    stack->tcp_syn_received = 0;
    stack->tcp_syn_ack_sent = 0;
    stack->tcp_fin_received = 0;
    stack->tcp_rst_received = 0;
    stack->tcp_rst_sent = 0;
    stack->tcp_unknown_port_frames = 0;
    stack->tcp_tuple_mismatches = 0;
    stack->tcp_checksum_drops = 0;
    stack->tcp_duplicate_acks = 0;
    stack->tcp_retransmissions = 0;
    stack->tcp_retransmission_backoffs = 0;
    stack->tcp_timeouts = 0;
    stack->tcp_receive_window_drops = 0;
    stack->tcp_window_updates = 0;
    return 1;
}

struct arwill_tcp_stream *arwill_ipv4_remote_stream(
    struct arwill_ipv4_stack *stack) {
    return stack == 0 ? 0 : &stack->endpoints[0].stream;
}

const struct arwill_tcp_stream *arwill_ipv4_remote_stream_const(
    const struct arwill_ipv4_stack *stack) {
    return stack == 0 ? 0 : &stack->endpoints[0].stream;
}

int arwill_ipv4_tcp_endpoint_snapshot(
    const struct arwill_ipv4_stack *stack,
    size_t index,
    struct arwill_tcp_endpoint_snapshot *snapshot
) {
    if (stack == 0 || snapshot == 0 || index >= arwill_tcp_endpoint_capacity) {
        return 0;
    }

    const struct arwill_tcp_endpoint *endpoint = &stack->endpoints[index];
    const struct arwill_tcp_stream *stream = &endpoint->stream;
    snapshot->allocated = endpoint->allocated;
    snapshot->listening = endpoint->allocated && stream->listening;
    snapshot->connected = endpoint->allocated &&
        arwill_tcp_stream_connected(stream);
    snapshot->owner = index == 0U ? "remote-console" : "application";
    if (!endpoint->allocated) {
        snapshot->state = "free";
    } else if (endpoint->connect_failed) {
        snapshot->state = "connect-failed";
    } else if (endpoint->connect_pending) {
        snapshot->state = "resolving-peer";
    } else if (!stream->listening) {
        snapshot->state = "idle";
    } else {
        snapshot->state = arwill_tcp_state_name(stream->listener.state);
    }
    snapshot->local_port = endpoint->bound_port;
    snapshot->peer_port = endpoint->connect_pending
        ? endpoint->connect_peer_port : stream->listener.peer_port;
    for (size_t address_index = 0; address_index < 4U; address_index++) {
        snapshot->peer_address[address_index] = endpoint->connect_pending
            ? endpoint->connect_peer_address[address_index]
            : stream->listener.peer_address[address_index];
    }
    snapshot->receive_available = arwill_tcp_stream_receive_capacity -
        stream->receive_count;
    snapshot->peer_window = stream->listener.peer_window;
    snapshot->peer_maximum_segment_size =
        stream->listener.peer_maximum_segment_size;
    snapshot->pending_segments = endpoint->tcp_pending_count;
    snapshot->smoothed_round_trip_ms = endpoint->tcp_smoothed_round_trip_ms;
    snapshot->retransmission_timeout_ms =
        endpoint->tcp_retransmission_timeout_ms;
    snapshot->connections = stream->connections;
    snapshot->disconnects = stream->disconnects;
    snapshot->bytes_received = stream->bytes_received;
    snapshot->bytes_sent = stream->bytes_sent;
    snapshot->bytes_dropped = stream->bytes_dropped;
    snapshot->send_failures = stream->send_failures;
    return 1;
}

struct arwill_tcp_stream *arwill_ipv4_tcp_open(struct arwill_ipv4_stack *stack) {
    if (stack == 0) {
        return 0;
    }
    for (size_t index = 1U; index < arwill_tcp_endpoint_capacity; index++) {
        if (!stack->endpoints[index].allocated) {
            initialize_tcp_endpoint(&stack->endpoints[index]);
            stack->endpoints[index].allocated = 1;
            return &stack->endpoints[index].stream;
        }
    }
    return 0;
}

static struct arwill_tcp_endpoint *endpoint_for_stream(
    struct arwill_ipv4_stack *stack, struct arwill_tcp_stream *stream) {
    if (stack == 0 || stream == 0) {
        return 0;
    }
    for (size_t index = 0; index < arwill_tcp_endpoint_capacity; index++) {
        if (&stack->endpoints[index].stream == stream &&
            stack->endpoints[index].allocated) {
            return &stack->endpoints[index];
        }
    }
    return 0;
}

static int local_port_available(const struct arwill_ipv4_stack *stack,
    const struct arwill_tcp_endpoint *endpoint, uint16_t port) {
    for (size_t index = 0; index < arwill_tcp_endpoint_capacity; index++) {
        const struct arwill_tcp_endpoint *candidate = &stack->endpoints[index];
        if (candidate != endpoint && candidate->allocated &&
            candidate->bound_port == port) {
            return 0;
        }
    }
    return 1;
}

int arwill_ipv4_tcp_bind(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, uint16_t port) {
    struct arwill_tcp_endpoint *endpoint = endpoint_for_stream(stack, stream);
    if (endpoint == 0 || port == 0U || endpoint->bound_port != 0U ||
        !local_port_available(stack, endpoint, port)) {
        return 0;
    }
    endpoint->bound_port = port;
    return 1;
}

int arwill_ipv4_tcp_listen(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, uint16_t port) {
    struct arwill_tcp_endpoint *endpoint = endpoint_for_stream(stack, stream);
    if (endpoint == 0 || port == 0U ||
        (endpoint->bound_port != 0U && endpoint->bound_port != port)) {
        return 0;
    }
    if (endpoint->bound_port == 0U &&
        !arwill_ipv4_tcp_bind(stack, stream, port)) {
        return 0;
    }
    reset_tcp_stream(endpoint);
    endpoint->bound_port = port;
    endpoint->connect_failed = 0;
    endpoint->active_open = 0;
    return arwill_tcp_stream_listen(
        stream, port, 0x41520000U + stream->connections
    );
}

int arwill_ipv4_tcp_connect(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, const uint8_t peer_address[4],
    uint16_t local_port, uint16_t peer_port) {
    struct arwill_tcp_endpoint *endpoint = endpoint_for_stream(stack, stream);
    if (endpoint == 0 || peer_address == 0 || local_port == 0U ||
        peer_port == 0U || endpoint->bound_port != local_port) {
        return -1;
    }
    if (arwill_tcp_stream_connected(stream)) {
        return 1;
    }
    if (endpoint->connect_failed) {
        return -1;
    }
    if (endpoint->connect_pending ||
        stream->listener.state == arwill_tcp_state_syn_sent) {
        return 0;
    }

    reset_tcp_stream(endpoint);
    endpoint->bound_port = local_port;
    stream->listening = 1;
    stream->listener.port = local_port;
    endpoint->connect_local_port = local_port;
    endpoint->connect_peer_port = peer_port;
    for (size_t index = 0; index < 4U; index++) {
        endpoint->connect_peer_address[index] = peer_address[index];
        endpoint->connect_next_hop[index] =
            peer_address[0] == stack->address[0] &&
            peer_address[1] == stack->address[1] &&
            peer_address[2] == stack->address[2]
                ? peer_address[index] : stack->gateway[index];
    }
    endpoint->connect_pending = 1;
    endpoint->active_open = 1;
    endpoint->connect_failed = 0;
    endpoint->connect_arp_attempts = 0U;
    endpoint->connect_arp_sent_milliseconds = 0U;
    return 0;
}

int arwill_ipv4_tcp_connect_status(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream) {
    struct arwill_tcp_endpoint *endpoint = endpoint_for_stream(stack, stream);
    if (endpoint == 0 || endpoint->connect_failed) {
        return -1;
    }
    return arwill_tcp_stream_connected(stream) ? 1 : 0;
}

void arwill_ipv4_tcp_release(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream) {
    struct arwill_tcp_endpoint *endpoint = endpoint_for_stream(stack, stream);
    if (endpoint == 0 || endpoint == &stack->endpoints[0]) {
        return;
    }
    arwill_tcp_stream_stop(&endpoint->stream);
    initialize_tcp_endpoint(endpoint);
}

int arwill_ipv4_send_arp_request(const struct arwill_ipv4_stack *stack,
    const uint8_t target[4]) {
    uint8_t frame[60];

    if (stack == 0 || stack->network == 0 || target == 0) {
        return 0;
    }
    for (size_t index = 0; index < sizeof(frame); index++) {
        frame[index] = 0;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = broadcast_mac[index];
        frame[index + arwill_network_mac_length] = stack->mac[index];
    }
    put16(frame, 12U, 0x0806U);
    put16(frame, 14U, 1U);
    put16(frame, 16U, 0x0800U);
    frame[18] = 6;
    frame[19] = 4;
    put16(frame, 20U, 1U);
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[22U + index] = stack->mac[index];
        frame[32U + index] = 0;
    }
    for (size_t index = 0; index < 4U; index++) {
        frame[28U + index] = stack->address[index];
        frame[38U + index] = target[index];
    }
    return arwill_network_send_frame(stack->network, frame, sizeof(frame));
}

static int send_arp_reply(struct arwill_ipv4_stack *stack, const uint8_t *request) {
    uint8_t frame[60];

    for (size_t index = 0; index < sizeof(frame); index++) {
        frame[index] = 0;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = request[22U + index];
        frame[6U + index] = stack->mac[index];
        frame[22U + index] = stack->mac[index];
        frame[32U + index] = request[22U + index];
    }
    put16(frame, 12U, 0x0806U);
    put16(frame, 14U, 1U);
    put16(frame, 16U, 0x0800U);
    frame[18] = 6U;
    frame[19] = 4U;
    put16(frame, 20U, 2U);
    for (size_t index = 0; index < 4U; index++) {
        frame[28U + index] = stack->address[index];
        frame[38U + index] = request[28U + index];
    }
    return arwill_network_send_frame(stack->network, frame, sizeof(frame));
}

static int send_echo_reply(struct arwill_ipv4_stack *stack,
    const uint8_t *request, size_t request_length) {
    uint8_t frame[arwill_network_frame_capacity];
    const uint8_t *request_ip = request + 14U;
    const size_t ip_header_length = (size_t)(request_ip[0] & 0x0fU) * 4U;
    const size_t ip_length = get16(request_ip, 2U);
    const size_t frame_length = 14U + ip_length;

    if ((request_ip[0] >> 4U) != 4U || ip_header_length < 20U ||
        ip_length < ip_header_length + 8U || frame_length > request_length ||
        get16(request_ip, 6U) & 0x3fffU || request_ip[9] != 1U ||
        !same_bytes(request_ip + 16U, stack->address, 4U)) {
        return 0;
    }
    if (checksum(request_ip, ip_header_length) != 0U) {
        stack->icmp_checksum_drops++;
        return 0;
    }

    const size_t icmp_length = ip_length - ip_header_length;
    const uint8_t *request_icmp = request_ip + ip_header_length;
    if (request_icmp[0] != 8U || request_icmp[1] != 0U) {
        return 0;
    }
    if (checksum(request_icmp, icmp_length) != 0U) {
        stack->icmp_checksum_drops++;
        return 0;
    }
    stack->icmp_echo_requests_received++;

    for (size_t index = 0; index < sizeof(frame); index++) {
        frame[index] = 0U;
    }
    for (size_t index = 0; index < frame_length; index++) {
        frame[index] = request[index];
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = request[6U + index];
        frame[6U + index] = stack->mac[index];
    }

    uint8_t *ip = frame + 14U;
    for (size_t index = 0; index < 4U; index++) {
        ip[12U + index] = stack->address[index];
        ip[16U + index] = request_ip[12U + index];
    }
    ip[8] = 64U;
    put16(ip, 10U, 0U);
    put16(ip, 10U, checksum(ip, ip_header_length));

    uint8_t *icmp = ip + ip_header_length;
    icmp[0] = 0U;
    put16(icmp, 2U, 0U);
    put16(icmp, 2U, checksum(icmp, icmp_length));
    if (!arwill_network_send_frame(stack->network, frame,
            frame_length < 60U ? 60U : frame_length)) {
        return 0;
    }
    stack->icmp_echo_replies_sent++;
    return 1;
}

static int resolve_gateway(struct arwill_ipv4_stack *stack) {
    uint8_t frame[arwill_network_frame_capacity];
    size_t length = 0;

    if (stack->gateway_resolved) {
        return 1;
    }
    if (!arwill_ipv4_send_arp_request(stack, stack->gateway)) {
        return 0;
    }
    for (size_t attempt = 0; attempt < 4096U; attempt++) {
        if (!arwill_network_poll_frame(stack->network, frame, sizeof(frame), &length)) {
            if ((attempt % 64U) == 63U) {
                arwill_cpu_wait_for_interrupt();
            }
            continue;
        }
        if (length < 42U || get16(frame, 12U) != 0x0806U ||
            get16(frame, 14U) != 1U || get16(frame, 16U) != 0x0800U ||
            frame[18] != 6U || frame[19] != 4U || get16(frame, 20U) != 2U ||
            !same_bytes(frame + 28U, stack->gateway, 4U) ||
            !same_bytes(frame + 38U, stack->address, 4U)) {
            continue;
        }
        for (size_t index = 0; index < arwill_network_mac_length; index++) {
            stack->gateway_mac[index] = frame[22U + index];
        }
        stack->gateway_resolved = 1;
        return 1;
    }
    return 0;
}

static int send_echo(struct arwill_ipv4_stack *stack) {
    uint8_t frame[60];
    uint8_t *ip = frame + 14U;
    uint8_t *icmp = ip + 20U;

    for (size_t index = 0; index < sizeof(frame); index++) {
        frame[index] = 0;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = stack->gateway_mac[index];
        frame[index + arwill_network_mac_length] = stack->mac[index];
    }
    put16(frame, 12U, 0x0800U);
    ip[0] = 0x45U;
    put16(ip, 2U, 28U);
    put16(ip, 4U, stack->echo_sequence);
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 1U;
    for (size_t index = 0; index < 4U; index++) {
        ip[12U + index] = stack->address[index];
        ip[16U + index] = stack->gateway[index];
    }
    put16(ip, 10U, checksum(ip, 20U));
    icmp[0] = 8U;
    put16(icmp, 4U, stack->echo_identifier);
    put16(icmp, 6U, stack->echo_sequence);
    put16(icmp, 2U, checksum(icmp, 8U));
    stack->echo_sequence++;
    return arwill_network_send_frame(stack->network, frame, sizeof(frame));
}

int arwill_ipv4_ping_gateway(struct arwill_ipv4_stack *stack) {
    uint8_t frame[arwill_network_frame_capacity];
    size_t length = 0;

    if (stack == 0 || !resolve_gateway(stack) || !send_echo(stack)) {
        return 0;
    }
    for (size_t attempt = 0; attempt < 4096U; attempt++) {
        if (!arwill_network_poll_frame(stack->network, frame, sizeof(frame), &length)) {
            if ((attempt % 64U) == 63U) {
                arwill_cpu_wait_for_interrupt();
            }
            continue;
        }
        if (length < 42U || get16(frame, 12U) != 0x0800U || frame[14] != 0x45U ||
            frame[23] != 1U || frame[34] != 0U || frame[35] != 0U ||
            get16(frame, 38U) != stack->echo_identifier ||
            get16(frame, 40U) != (uint16_t)(stack->echo_sequence - 1U)) {
            continue;
        }
        return 1;
    }
    return 0;
}

static uint16_t tcp_checksum(const uint8_t *ip, const uint8_t *tcp, size_t length) {
    uint32_t sum = 6U + (uint32_t)length;

    sum += get16(ip, 12U);
    sum += get16(ip, 14U);
    sum += get16(ip, 16U);
    sum += get16(ip, 18U);
    for (size_t index = 0; index + 1U < length; index += 2U) {
        sum += get16(tcp, index);
    }
    if ((length & 1U) != 0U) {
        sum += (uint32_t)tcp[length - 1U] << 8U;
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xffffU) + (sum >> 16U);
    }
    return (uint16_t)~sum;
}

static uint16_t advertised_receive_window(
    const struct arwill_tcp_endpoint *endpoint) {
    const size_t available = arwill_tcp_stream_receive_capacity -
        endpoint->stream.receive_count;
    return available > UINT16_MAX ? UINT16_MAX : (uint16_t)available;
}

static int send_tcp_segment_to(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint,
    const struct arwill_tcp_segment *segment, const uint8_t *payload,
    size_t payload_length, const uint8_t destination_mac[arwill_network_mac_length]) {
    uint8_t frame[arwill_network_frame_capacity];
    uint8_t *ip = frame + 14U;
    uint8_t *tcp = ip + 20U;
    const size_t tcp_header_length =
        (segment->flags & arwill_tcp_flag_syn) != 0U ? 24U : 20U;

    if (segment->flags == 0U ||
        payload_length > sizeof(frame) - 14U - 20U - tcp_header_length) {
        return 1;
    }
    for (size_t index = 0; index < sizeof(frame); index++) {
        frame[index] = 0;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = destination_mac[index];
        frame[6U + index] = stack->mac[index];
    }
    put16(frame, 12U, 0x0800U);
    ip[0] = 0x45U;
    put16(ip, 2U, (uint16_t)(20U + tcp_header_length + payload_length));
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 6U;
    for (size_t index = 0; index < 4U; index++) {
        ip[12U + index] = segment->source_address[index];
        ip[16U + index] = segment->destination_address[index];
    }
    put16(ip, 10U, checksum(ip, 20U));
    put16(tcp, 0U, segment->source_port);
    put16(tcp, 2U, segment->destination_port);
    put32(tcp, 4U, segment->sequence);
    put32(tcp, 8U, segment->acknowledgement);
    tcp[12] = (uint8_t)((tcp_header_length / 4U) << 4U);
    tcp[13] = segment->flags;
    const uint16_t window = endpoint == 0 ? 0U :
        advertised_receive_window(endpoint);
    put16(tcp, 14U, window);
    if (tcp_header_length == 24U) {
        tcp[20] = 2U;
        tcp[21] = 4U;
        put16(tcp, 22U, (uint16_t)arwill_tcp_local_maximum_segment_size);
    }
    for (size_t index = 0; index < payload_length; index++) {
        tcp[tcp_header_length + index] = payload[index];
    }
    put16(tcp, 16U, tcp_checksum(ip, tcp, tcp_header_length + payload_length));
    const size_t frame_length = 14U + 20U + tcp_header_length + payload_length;
    if (!arwill_network_send_frame(stack->network, frame,
            frame_length < 60U ? 60U : frame_length)) {
        return 0;
    }
    stack->tcp_frames_sent++;
    stack->tcp_bytes_sent += (uint32_t)payload_length;
    if (endpoint != 0 &&
        segment->source_port == endpoint->stream.listener.port &&
        segment->destination_port == endpoint->stream.listener.peer_port) {
        endpoint->tcp_last_advertised_window = window;
    }
    return 1;
}

static int send_tcp_segment(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint,
    const struct arwill_tcp_segment *segment, const uint8_t *payload,
    size_t payload_length) {
    return send_tcp_segment_to(
        stack, endpoint, segment, payload, payload_length, endpoint->tcp_peer_mac
    );
}

static void reset_tcp_stream(struct arwill_tcp_endpoint *endpoint) {
    endpoint->stream.bytes_dropped +=
        (uint32_t)endpoint->stream.transmit_count;
    arwill_tcp_listener_reset(
        &endpoint->stream.listener,
        0x41520000U + endpoint->stream.connections
    );
    endpoint->stream.receive_head = 0;
    endpoint->stream.receive_count = 0;
    endpoint->stream.transmit_head = 0;
    endpoint->stream.transmit_count = 0;
    endpoint->stream.peer_closed = 0;
    endpoint->stream.close_requested = 0;
    endpoint->stream.receive_window_changed = 0;
    for (size_t index = 0; index < arwill_tcp_pending_segment_capacity; index++) {
        endpoint->tcp_pending[index].active = 0;
        endpoint->tcp_pending[index].payload_length = 0;
        endpoint->tcp_pending[index].retransmissions = 0;
    }
    endpoint->tcp_pending_head = 0;
    endpoint->tcp_pending_count = 0;
    endpoint->tcp_close_started_milliseconds = 0;
    endpoint->tcp_time_wait_started_milliseconds = 0;
    endpoint->tcp_window_update_pending = 0;
    endpoint->connect_pending = 0;
    endpoint->connect_arp_sent_milliseconds = 0U;
    endpoint->connect_arp_attempts = 0U;
    endpoint->active_open = 0;
}

static int remember_pending_segment(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint,
    const struct arwill_tcp_segment *segment, const uint8_t *payload,
    size_t payload_length) {
    if (stack == 0 || segment == 0
        || payload_length > arwill_tcp_pending_payload_capacity
        || endpoint->tcp_pending_count >= arwill_tcp_pending_segment_capacity
        || (payload == 0 && payload_length != 0U)) {
        return 0;
    }

    const size_t pending_index = (endpoint->tcp_pending_head +
        endpoint->tcp_pending_count) % arwill_tcp_pending_segment_capacity;
    struct arwill_tcp_pending_segment *pending = &endpoint->tcp_pending[pending_index];
    pending->segment = *segment;
    for (size_t index = 0; index < payload_length; index++) {
        pending->payload[index] = payload[index];
    }
    pending->payload_length = payload_length;
    pending->expected_acknowledgement = segment->sequence
        + (uint32_t)payload_length;
    if ((segment->flags & arwill_tcp_flag_syn) != 0U
        || (segment->flags & arwill_tcp_flag_fin) != 0U) {
        pending->expected_acknowledgement++;
    }
    pending->sent_at_milliseconds =
        arwill_clock_monotonic_milliseconds(stack->clock);
    pending->retransmission_timeout_ms = endpoint->tcp_retransmission_timeout_ms;
    pending->retransmissions = 0;
    pending->active = 1;
    endpoint->tcp_pending_count++;
    return 1;
}

static int start_active_syn(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    struct arwill_tcp_segment syn;
    if (!arwill_tcp_listener_connect(
            &endpoint->stream.listener,
            stack->address,
            endpoint->connect_local_port,
            endpoint->connect_peer_address,
            endpoint->connect_peer_port,
            0x41524000U + endpoint->stream.connections,
            &syn
        ) || !send_tcp_segment(stack, endpoint, &syn, 0, 0U) ||
        !remember_pending_segment(stack, endpoint, &syn, 0, 0U)) {
        endpoint->connect_failed = 1;
        endpoint->connect_pending = 0;
        return 0;
    }
    endpoint->connect_pending = 0;
    return 1;
}

static void maintain_active_connect(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    if (!endpoint->connect_pending) {
        return;
    }
    const uint64_t now = arwill_clock_monotonic_milliseconds(stack->clock);
    if (endpoint->connect_arp_attempts != 0U &&
        now - endpoint->connect_arp_sent_milliseconds < arwill_tcp_arp_retry_ms) {
        return;
    }
    if (endpoint->connect_arp_attempts >= arwill_tcp_arp_max_attempts) {
        reset_tcp_stream(endpoint);
        endpoint->connect_failed = 1;
        return;
    }
    if (arwill_ipv4_send_arp_request(stack, endpoint->connect_next_hop)) {
        endpoint->connect_arp_sent_milliseconds = now;
        endpoint->connect_arp_attempts++;
    }
}

static int handle_arp_reply(struct arwill_ipv4_stack *stack,
    const uint8_t *frame, size_t length) {
    if (length < 42U || get16(frame, 20U) != 2U ||
        !same_bytes(frame + 38U, stack->address, 4U)) {
        return 0;
    }
    if (same_bytes(frame + 28U, stack->gateway, 4U)) {
        for (size_t index = 0; index < arwill_network_mac_length; index++) {
            stack->gateway_mac[index] = frame[22U + index];
        }
        stack->gateway_resolved = 1;
    }
    int matched = 0;
    for (size_t endpoint_index = 0;
        endpoint_index < arwill_tcp_endpoint_capacity; endpoint_index++) {
        struct arwill_tcp_endpoint *endpoint =
            &stack->endpoints[endpoint_index];
        if (!endpoint->allocated || !endpoint->connect_pending ||
            !same_bytes(frame + 28U, endpoint->connect_next_hop, 4U)) {
            continue;
        }
        for (size_t index = 0; index < arwill_network_mac_length; index++) {
            endpoint->tcp_peer_mac[index] = frame[22U + index];
        }
        (void)start_active_syn(stack, endpoint);
        matched = 1;
    }
    return matched;
}

static void update_retransmission_timeout(struct arwill_tcp_endpoint *endpoint,
    uint64_t sample) {
    if (sample == 0U) {
        sample = 1U;
    }
    if (endpoint->tcp_smoothed_round_trip_ms == 0U) {
        endpoint->tcp_smoothed_round_trip_ms = sample;
        endpoint->tcp_round_trip_variance_ms = sample / 2U;
    } else {
        const uint64_t difference = endpoint->tcp_smoothed_round_trip_ms > sample
            ? endpoint->tcp_smoothed_round_trip_ms - sample
            : sample - endpoint->tcp_smoothed_round_trip_ms;
        endpoint->tcp_round_trip_variance_ms =
            (3U * endpoint->tcp_round_trip_variance_ms + difference) / 4U;
        endpoint->tcp_smoothed_round_trip_ms =
            (7U * endpoint->tcp_smoothed_round_trip_ms + sample) / 8U;
    }
    uint64_t variance = 4U * endpoint->tcp_round_trip_variance_ms;
    if (variance < 10U) {
        variance = 10U;
    }
    uint64_t timeout = endpoint->tcp_smoothed_round_trip_ms + variance;
    if (timeout < arwill_tcp_retransmission_minimum_ms) {
        timeout = arwill_tcp_retransmission_minimum_ms;
    }
    if (timeout > arwill_tcp_retransmission_maximum_ms) {
        timeout = arwill_tcp_retransmission_maximum_ms;
    }
    endpoint->tcp_retransmission_timeout_ms = timeout;
}

static void acknowledge_pending_segment(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint,
    const struct arwill_tcp_segment *incoming) {
    if ((incoming->flags & arwill_tcp_flag_ack) == 0U) {
        return;
    }
    const uint64_t now = arwill_clock_monotonic_milliseconds(stack->clock);
    while (endpoint->tcp_pending_count != 0U) {
        struct arwill_tcp_pending_segment *pending =
            &endpoint->tcp_pending[endpoint->tcp_pending_head];
        if (incoming->acknowledgement != pending->expected_acknowledgement &&
            !arwill_tcp_sequence_after(
                incoming->acknowledgement, pending->expected_acknowledgement)) {
            break;
        }
        if (pending->retransmissions == 0U &&
            now >= pending->sent_at_milliseconds) {
            update_retransmission_timeout(
                endpoint, now - pending->sent_at_milliseconds
            );
        }
        pending->active = 0;
        pending->payload_length = 0;
        pending->retransmissions = 0;
        endpoint->tcp_pending_head = (endpoint->tcp_pending_head + 1U) %
            arwill_tcp_pending_segment_capacity;
        endpoint->tcp_pending_count--;
    }
}

static void maintain_pending_segment(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    if (stack == 0 || endpoint->tcp_pending_count == 0U) {
        return;
    }

    const uint64_t now = arwill_clock_monotonic_milliseconds(stack->clock);
    for (size_t offset = 0; offset < endpoint->tcp_pending_count; offset++) {
        const size_t pending_index = (endpoint->tcp_pending_head + offset) %
            arwill_tcp_pending_segment_capacity;
        struct arwill_tcp_pending_segment *pending =
            &endpoint->tcp_pending[pending_index];
        if (now - pending->sent_at_milliseconds <
            pending->retransmission_timeout_ms) {
            continue;
        }
        if (pending->retransmissions >= arwill_tcp_max_retransmissions) {
            const int was_connected = arwill_tcp_stream_connected(&endpoint->stream);
            const int was_active_open = endpoint->active_open;
            stack->tcp_timeouts++;
            if (was_connected) {
                endpoint->stream.disconnects++;
            }
            reset_tcp_stream(endpoint);
            if (was_active_open) {
                endpoint->connect_failed = 1;
            }
            return;
        }
        if (!send_tcp_segment(stack, endpoint, &pending->segment,
                pending->payload, pending->payload_length)) {
            endpoint->stream.send_failures++;
        }
        pending->sent_at_milliseconds = now;
        pending->retransmissions++;
        if (pending->retransmission_timeout_ms <
            arwill_tcp_retransmission_maximum_ms / 2U) {
            pending->retransmission_timeout_ms *= 2U;
        } else {
            pending->retransmission_timeout_ms =
                arwill_tcp_retransmission_maximum_ms;
        }
        stack->tcp_retransmissions++;
        stack->tcp_retransmission_backoffs++;
    }
}

static void maintain_tcp_close(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    if (stack == 0) {
        return;
    }
    const uint64_t now = arwill_clock_monotonic_milliseconds(stack->clock);
    if (endpoint->stream.listener.state != arwill_tcp_state_listen &&
        endpoint->stream.listener.state != arwill_tcp_state_syn_sent &&
        endpoint->stream.listener.state != arwill_tcp_state_syn_received &&
        endpoint->stream.listener.state != arwill_tcp_state_established &&
        endpoint->stream.listener.state != arwill_tcp_state_close_wait &&
        endpoint->stream.listener.state != arwill_tcp_state_time_wait) {
        if (endpoint->tcp_close_started_milliseconds == 0U) {
            endpoint->tcp_close_started_milliseconds = now;
        }
        if (now - endpoint->tcp_close_started_milliseconds >=
            arwill_tcp_close_timeout_ms) {
            reset_tcp_stream(endpoint);
        }
        return;
    }
    if (endpoint->stream.listener.state != arwill_tcp_state_time_wait) {
        return;
    }
    if (endpoint->tcp_time_wait_started_milliseconds == 0U) {
        endpoint->tcp_time_wait_started_milliseconds = now;
    }
    if (now - endpoint->tcp_time_wait_started_milliseconds >= arwill_tcp_time_wait_ms) {
        reset_tcp_stream(endpoint);
    }
}

static void remember_tcp_peer(struct arwill_tcp_endpoint *endpoint,
    const uint8_t *frame) {
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        endpoint->tcp_peer_mac[index] = frame[6U + index];
    }
}

static void enqueue_stream_receive(struct arwill_tcp_endpoint *endpoint,
    const uint8_t *payload, size_t payload_length) {
    for (size_t index = 0; index < payload_length; index++) {
        if (endpoint->stream.receive_count
            >= arwill_tcp_stream_receive_capacity) {
            endpoint->stream.bytes_dropped++;
            continue;
        }
        const size_t tail = (endpoint->stream.receive_head
            + endpoint->stream.receive_count)
            % arwill_tcp_stream_receive_capacity;
        endpoint->stream.receive[tail] = payload[index];
        endpoint->stream.receive_count++;
        endpoint->stream.bytes_received++;
    }
}

static int send_current_ack(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    struct arwill_tcp_segment acknowledgement;
    for (size_t index = 0; index < 4U; index++) {
        acknowledgement.source_address[index] =
            endpoint->stream.listener.local_address[index];
        acknowledgement.destination_address[index] =
            endpoint->stream.listener.peer_address[index];
    }
    acknowledgement.source_port = endpoint->stream.listener.port;
    acknowledgement.destination_port = endpoint->stream.listener.peer_port;
    acknowledgement.sequence = endpoint->stream.listener.sequence;
    acknowledgement.acknowledgement = endpoint->stream.listener.acknowledgement;
    acknowledgement.flags = arwill_tcp_flag_ack;
    acknowledgement.window = advertised_receive_window(endpoint);
    acknowledgement.maximum_segment_size = 0U;
    acknowledgement.payload_length = 0U;
    return send_tcp_segment(stack, endpoint, &acknowledgement, 0, 0U);
}

static void maintain_window_update(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    if (stack != 0 && endpoint->stream.receive_window_changed) {
        if (endpoint->tcp_last_advertised_window == 0U &&
            advertised_receive_window(endpoint) != 0U) {
            endpoint->tcp_window_update_pending = 1;
        }
        endpoint->stream.receive_window_changed = 0;
    }
    if (stack == 0 || !endpoint->tcp_window_update_pending ||
        !arwill_tcp_stream_connected(&endpoint->stream)) {
        return;
    }
    if (send_current_ack(stack, endpoint)) {
        endpoint->tcp_window_update_pending = 0;
        stack->tcp_window_updates++;
    }
}

static void maintain_stream_close(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    if (stack == 0) {
        return;
    }
    if (!endpoint->stream.listening) {
        if (endpoint->tcp_pending_count != 0U ||
            endpoint->stream.listener.state != arwill_tcp_state_listen) {
            reset_tcp_stream(endpoint);
        }
        return;
    }
    if (endpoint->stream.close_requested) {
        const uint64_t now =
            arwill_clock_monotonic_milliseconds(stack->clock);
        if (endpoint->tcp_close_started_milliseconds == 0U) {
            endpoint->tcp_close_started_milliseconds = now;
        } else if (now - endpoint->tcp_close_started_milliseconds >=
            arwill_tcp_close_timeout_ms) {
            reset_tcp_stream(endpoint);
            return;
        }
    }
    if (!endpoint->stream.close_requested ||
        !arwill_tcp_stream_connected(&endpoint->stream) ||
        endpoint->stream.transmit_count != 0U || endpoint->tcp_pending_count != 0U) {
        return;
    }
    struct arwill_tcp_segment segment;
    if (!arwill_tcp_listener_begin_close(&endpoint->stream.listener, &segment) ||
        !send_tcp_segment(stack, endpoint, &segment, 0, 0U) ||
        !remember_pending_segment(stack, endpoint, &segment, 0, 0U)) {
        endpoint->stream.send_failures++;
        reset_tcp_stream(endpoint);
        return;
    }
    endpoint->stream.close_requested = 0;
    endpoint->stream.disconnects++;
    if (endpoint->tcp_close_started_milliseconds == 0U) {
        endpoint->tcp_close_started_milliseconds =
            arwill_clock_monotonic_milliseconds(stack->clock);
    }
}

static int flush_stream_transmit(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_endpoint *endpoint) {
    if (!arwill_tcp_stream_connected(&endpoint->stream)) {
        return 1;
    }
    while (endpoint->tcp_pending_count < arwill_tcp_pending_segment_capacity &&
        endpoint->stream.transmit_count != 0U &&
        endpoint->stream.listener.peer_window != 0U) {
        uint8_t payload[arwill_tcp_pending_payload_capacity];
        size_t length = endpoint->stream.transmit_count;
        if (length > sizeof(payload)) {
            length = sizeof(payload);
        }
        if (length > endpoint->stream.listener.peer_maximum_segment_size) {
            length = endpoint->stream.listener.peer_maximum_segment_size;
        }
        if (length > endpoint->stream.listener.peer_window) {
            length = endpoint->stream.listener.peer_window;
        }
        if (length == 0U) {
            break;
        }
        for (size_t index = 0; index < length; index++) {
            payload[index] = endpoint->stream.transmit[
                (endpoint->stream.transmit_head + index) %
                    arwill_tcp_stream_transmit_capacity
            ];
        }

        struct arwill_tcp_segment segment;
        for (size_t index = 0; index < 4U; index++) {
            segment.source_address[index] = endpoint->stream.listener.local_address[index];
            segment.destination_address[index] = endpoint->stream.listener.peer_address[index];
        }
        segment.source_port = endpoint->stream.listener.port;
        segment.destination_port = endpoint->stream.listener.peer_port;
        segment.sequence = endpoint->stream.listener.sequence;
        segment.acknowledgement = endpoint->stream.listener.acknowledgement;
        segment.flags = arwill_tcp_flag_ack | arwill_tcp_flag_psh;
        segment.window = advertised_receive_window(endpoint);
        segment.maximum_segment_size = 0U;
        segment.payload_length = length;
        if (!send_tcp_segment(stack, endpoint, &segment, payload, length) ||
            !remember_pending_segment(
                stack, endpoint, &segment, payload, length)) {
            endpoint->stream.send_failures++;
            return 0;
        }
        endpoint->stream.listener.sequence += (uint32_t)length;
        endpoint->stream.listener.peer_window =
            (uint16_t)(endpoint->stream.listener.peer_window - length);
        endpoint->stream.bytes_sent += (uint32_t)length;
        endpoint->stream.transmit_head =
            (endpoint->stream.transmit_head + length) %
                arwill_tcp_stream_transmit_capacity;
        endpoint->stream.transmit_count -= length;
    }
    return 1;
}

static int send_reset_for_segment(struct arwill_ipv4_stack *stack,
    const struct arwill_tcp_segment *incoming,
    const uint8_t destination_mac[arwill_network_mac_length]) {
    struct arwill_tcp_segment reset;
    if ((incoming->flags & arwill_tcp_flag_rst) != 0U) {
        return 0;
    }
    for (size_t index = 0; index < 4U; index++) {
        reset.source_address[index] = incoming->destination_address[index];
        reset.destination_address[index] = incoming->source_address[index];
    }
    reset.source_port = incoming->destination_port;
    reset.destination_port = incoming->source_port;
    reset.payload_length = 0;
    if ((incoming->flags & arwill_tcp_flag_ack) != 0U) {
        reset.sequence = incoming->acknowledgement;
        reset.acknowledgement = 0U;
        reset.flags = arwill_tcp_flag_rst;
    } else {
        uint32_t consumed = (uint32_t)incoming->payload_length;
        if ((incoming->flags & arwill_tcp_flag_syn) != 0U) {
            consumed++;
        }
        if ((incoming->flags & arwill_tcp_flag_fin) != 0U) {
            consumed++;
        }
        reset.sequence = 0U;
        reset.acknowledgement = incoming->sequence + consumed;
    reset.flags = arwill_tcp_flag_rst | arwill_tcp_flag_ack;
    }
    reset.window = 0U;
    reset.maximum_segment_size = 0U;
    if (!send_tcp_segment_to(
            stack, 0, &reset, 0, 0U, destination_mac)) {
        return 0;
    }
    stack->tcp_rst_sent++;
    return 1;
}

static uint16_t parse_tcp_maximum_segment_size(const uint8_t *tcp,
    size_t tcp_header_length) {
    size_t offset = 20U;
    while (offset < tcp_header_length) {
        const uint8_t kind = tcp[offset];
        if (kind == 0U) {
            break;
        }
        if (kind == 1U) {
            offset++;
            continue;
        }
        if (offset + 1U >= tcp_header_length) {
            break;
        }
        const size_t option_length = tcp[offset + 1U];
        if (option_length < 2U || offset + option_length > tcp_header_length) {
            break;
        }
        if (kind == 2U && option_length == 4U) {
            return get16(tcp, offset + 2U);
        }
        offset += option_length;
    }
    return 0U;
}

int arwill_ipv4_poll_tcp(struct arwill_ipv4_stack *stack) {
    uint8_t frame[arwill_network_frame_capacity];
    size_t length = 0;
    if (stack == 0 || stack->network == 0) {
        return 0;
    }
    for (size_t index = 0; index < arwill_tcp_endpoint_capacity; index++) {
        struct arwill_tcp_endpoint *candidate = &stack->endpoints[index];
        if (!candidate->allocated) {
            continue;
        }
        maintain_active_connect(stack, candidate);
        if (candidate->stream.listening) {
            maintain_pending_segment(stack, candidate);
            maintain_tcp_close(stack, candidate);
            maintain_window_update(stack, candidate);
            (void)flush_stream_transmit(stack, candidate);
        }
        maintain_stream_close(stack, candidate);
    }
    if (!arwill_network_poll_frame(stack->network, frame, sizeof(frame), &length)) {
        return 0;
    }
    if (length >= 42U && get16(frame, 12U) == 0x0806U) {
        if (get16(frame, 20U) == 1U &&
            same_bytes(frame + 38U, stack->address, 4U)) {
            return send_arp_reply(stack, frame);
        }
        return handle_arp_reply(stack, frame, length);
    }
    if (length >= 42U && get16(frame, 12U) == 0x0800U &&
        (frame[14] >> 4U) == 4U && frame[23] == 1U) {
        return send_echo_reply(stack, frame, length);
    }
    if (length < 54U || get16(frame, 12U) != 0x0800U || frame[14] != 0x45U ||
        frame[23] != 6U || !same_bytes(frame + 30U, stack->address, 4U)) {
        return 0;
    }
    if (checksum(frame + 14U, 20U) != 0U) {
        stack->tcp_checksum_drops++;
        return 0;
    }
    const size_t tcp_header_length = (size_t)(frame[46] >> 4U) * 4U;
    const size_t ip_length = get16(frame, 16U);
    if (tcp_header_length < 20U || ip_length < 20U + tcp_header_length
        || 14U + ip_length > length) {
        return 0;
    }
    if (tcp_checksum(frame + 14U, frame + 34U, ip_length - 20U) != 0U) {
        stack->tcp_checksum_drops++;
        return 0;
    }
    const size_t payload_length = ip_length - 20U - tcp_header_length;
    const uint8_t *payload = frame + 14U + 20U + tcp_header_length;

    struct arwill_tcp_segment incoming;
    struct arwill_tcp_segment reply;
    for (size_t index = 0; index < 4U; index++) {
        incoming.source_address[index] = frame[26U + index];
        incoming.destination_address[index] = frame[30U + index];
    }
    incoming.source_port = get16(frame, 34U);
    incoming.destination_port = get16(frame, 36U);
    incoming.sequence = get32(frame, 38U);
    incoming.acknowledgement = get32(frame, 42U);
    incoming.flags = frame[47];
    incoming.window = get16(frame, 48U);
    incoming.maximum_segment_size = (incoming.flags & arwill_tcp_flag_syn) != 0U
        ? parse_tcp_maximum_segment_size(frame + 34U, tcp_header_length) : 0U;
    incoming.payload_length = payload_length;
    stack->tcp_frames_received++;
    stack->tcp_bytes_received += (uint32_t)payload_length;
    if ((incoming.flags & arwill_tcp_flag_syn) != 0U) {
        stack->tcp_syn_received++;
    }
    if ((incoming.flags & arwill_tcp_flag_fin) != 0U) {
        stack->tcp_fin_received++;
    }
    if ((incoming.flags & arwill_tcp_flag_rst) != 0U) {
        stack->tcp_rst_received++;
    }

    struct arwill_tcp_endpoint *endpoint = 0;
    for (size_t index = 0; index < arwill_tcp_endpoint_capacity; index++) {
        struct arwill_tcp_endpoint *candidate = &stack->endpoints[index];
        if (candidate->allocated && candidate->stream.listening &&
            incoming.destination_port == candidate->stream.listener.port) {
            endpoint = candidate;
            break;
        }
    }
    if (endpoint == 0) {
        stack->tcp_unknown_port_frames++;
        return send_reset_for_segment(stack, &incoming, frame + 6U);
    }
    const enum arwill_tcp_state dispatch_state = endpoint->stream.listener.state;
    const int closing_listener =
        dispatch_state == arwill_tcp_state_last_ack ||
        dispatch_state == arwill_tcp_state_fin_wait_1 ||
        dispatch_state == arwill_tcp_state_fin_wait_2 ||
        dispatch_state == arwill_tcp_state_closing ||
        dispatch_state == arwill_tcp_state_time_wait;
    if ((closing_listener || endpoint->stream.close_requested) &&
        (incoming.flags & arwill_tcp_flag_syn) != 0U &&
        !arwill_tcp_listener_matches(&endpoint->stream.listener, &incoming)) {
        reset_tcp_stream(endpoint);
    }
    if (!arwill_tcp_listener_matches(&endpoint->stream.listener, &incoming)) {
        stack->tcp_tuple_mismatches++;
        return 0;
    }

    const size_t receive_available = arwill_tcp_stream_receive_capacity -
        endpoint->stream.receive_count;
    if (payload_length > receive_available &&
        incoming.sequence == endpoint->stream.listener.acknowledgement) {
        stack->tcp_receive_window_drops++;
        return send_current_ack(stack, endpoint);
    }

    const enum arwill_tcp_state previous_state = endpoint->stream.listener.state;
    const uint32_t previous_acknowledgement = endpoint->stream.listener.acknowledgement;
    if (!arwill_tcp_listener_receive(&endpoint->stream.listener, &incoming, &reply)) {
        return 0;
    }
    acknowledge_pending_segment(stack, endpoint, &incoming);

    if (previous_state != arwill_tcp_state_listen &&
        endpoint->stream.listener.state == arwill_tcp_state_listen) {
        const int failed_active_open = endpoint->active_open;
        if ((incoming.flags & arwill_tcp_flag_rst) != 0U) {
            endpoint->stream.disconnects++;
        }
        reset_tcp_stream(endpoint);
        if (failed_active_open) {
            endpoint->connect_failed = 1;
        }
        return 1;
    }

    if (reply.flags == (arwill_tcp_flag_syn | arwill_tcp_flag_ack)) {
        remember_tcp_peer(endpoint, frame);
        stack->tcp_syn_ack_sent++;
    }

    if ((previous_state == arwill_tcp_state_syn_received ||
            previous_state == arwill_tcp_state_syn_sent)
        && endpoint->stream.listener.state == arwill_tcp_state_established) {
        endpoint->stream.connections++;
    }

    if (previous_state == arwill_tcp_state_established
        && incoming.sequence != previous_acknowledgement
        && reply.flags == arwill_tcp_flag_ack) {
        stack->tcp_duplicate_acks++;
    }

    if ((endpoint->stream.listener.state == arwill_tcp_state_established ||
            endpoint->stream.listener.state == arwill_tcp_state_close_wait)
        && payload_length != 0U
        && endpoint->stream.listener.acknowledgement != previous_acknowledgement) {
        enqueue_stream_receive(endpoint, payload, payload_length);
    }

    if ((incoming.flags & arwill_tcp_flag_fin) != 0U
        && endpoint->stream.listener.acknowledgement != previous_acknowledgement) {
        endpoint->stream.peer_closed = 1;
    }

    if (endpoint->stream.listener.state == arwill_tcp_state_time_wait &&
        previous_state != arwill_tcp_state_time_wait) {
        endpoint->tcp_time_wait_started_milliseconds =
            arwill_clock_monotonic_milliseconds(stack->clock);
    }

    if (!send_tcp_segment(stack, endpoint, &reply, 0, 0U)) {
        return 0;
    }
    if (reply.flags == (arwill_tcp_flag_syn | arwill_tcp_flag_ack) &&
        endpoint->tcp_pending_count == 0U &&
        !remember_pending_segment(stack, endpoint, &reply, 0, 0U)) {
        reset_tcp_stream(endpoint);
        return 0;
    }
    (void)flush_stream_transmit(stack, endpoint);
    return 1;
}

int arwill_ipv4_service_tcp(struct arwill_ipv4_stack *stack, size_t *frames_processed) {
    size_t processed = 0;

    if (frames_processed != 0) {
        *frames_processed = 0;
    }
    if (stack == 0 || stack->network == 0) {
        return 0;
    }
    for (size_t attempt = 0; attempt < 4096U; attempt++) {
        processed += (size_t)arwill_ipv4_poll_tcp(stack);
        if ((attempt % 64U) == 63U) {
            arwill_cpu_wait_for_interrupt();
        }
    }
    if (frames_processed != 0) {
        *frames_processed = processed;
    }
    return 1;
}

void arwill_ipv4_print_config(const struct arwill_ipv4_stack *stack,
    const struct arwill_console *console) {
    if (stack == 0 || console == 0) {
        return;
    }
    arwill_console_write(console, "ipv4: ");
    for (size_t index = 0; index < 4U; index++) {
        if (index != 0U) {
            arwill_console_write(console, ".");
        }
        char value[4];
        size_t length = 0;
        uint8_t number = stack->address[index];
        do {
            value[length++] = (char)('0' + number % 10U);
            number = (uint8_t)(number / 10U);
        } while (number != 0U);
        while (length != 0U) {
            length--;
            write_char(console, value[length]);
        }
    }
    arwill_console_write_line(console, "/24");
    arwill_console_write(console, "gateway: ");
    for (size_t index = 0; index < 4U; index++) {
        if (index != 0U) {
            arwill_console_write(console, ".");
        }
        uint8_t number = stack->gateway[index];
        if (number >= 100U) {
            write_char(console, (char)('0' + number / 100U));
            number = (uint8_t)(number % 100U);
        }
        if (number >= 10U || stack->gateway[index] >= 100U) {
            write_char(console, (char)('0' + number / 10U));
            number = (uint8_t)(number % 10U);
        }
        write_char(console, (char)('0' + number));
    }
    arwill_console_write_line(console, "");
}
