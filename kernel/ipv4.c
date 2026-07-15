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
    arwill_tcp_listener_init(
        &stack->tcp_listener,
        remote_console_port,
        0x41520000U
    );
    stack->remote_console_receive_head = 0;
    stack->remote_console_receive_count = 0;
    stack->remote_console_transmit_head = 0;
    stack->remote_console_transmit_count = 0;
    stack->remote_console_running = 1;
    stack->remote_console_peer_closed = 0;
    stack->tcp_pending.active = 0;
    stack->tcp_pending.payload_length = 0;
    stack->tcp_pending.expected_acknowledgement = 0;
    stack->tcp_pending.sent_at_milliseconds = 0;
    stack->tcp_pending.retransmissions = 0;
    stack->tcp_time_wait_started_milliseconds = 0;
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
    stack->tcp_timeouts = 0;
    stack->remote_console_connections = 0;
    stack->remote_console_disconnects = 0;
    stack->remote_console_bytes_received = 0;
    stack->remote_console_bytes_sent = 0;
    stack->remote_console_bytes_dropped = 0;
    stack->remote_console_send_failures = 0;
    return 1;
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

static int send_tcp_segment_to(struct arwill_ipv4_stack *stack,
    const struct arwill_tcp_segment *segment, const uint8_t *payload,
    size_t payload_length, const uint8_t destination_mac[arwill_network_mac_length]) {
    uint8_t frame[arwill_network_frame_capacity];
    uint8_t *ip = frame + 14U;
    uint8_t *tcp = ip + 20U;

    if (segment->flags == 0U || payload_length > sizeof(frame) - 54U) {
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
    put16(ip, 2U, (uint16_t)(40U + payload_length));
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
    tcp[12] = 0x50U;
    tcp[13] = segment->flags;
    put16(tcp, 14U, 4096U);
    for (size_t index = 0; index < payload_length; index++) {
        tcp[20U + index] = payload[index];
    }
    put16(tcp, 16U, tcp_checksum(ip, tcp, 20U + payload_length));
    const size_t frame_length = 14U + 20U + 20U + payload_length;
    if (!arwill_network_send_frame(stack->network, frame,
            frame_length < 60U ? 60U : frame_length)) {
        return 0;
    }
    stack->tcp_frames_sent++;
    stack->tcp_bytes_sent += (uint32_t)payload_length;
    return 1;
}

static int send_tcp_segment(struct arwill_ipv4_stack *stack,
    const struct arwill_tcp_segment *segment, const uint8_t *payload,
    size_t payload_length) {
    return send_tcp_segment_to(
        stack, segment, payload, payload_length, stack->tcp_peer_mac
    );
}

static void reset_remote_console(struct arwill_ipv4_stack *stack) {
    stack->remote_console_bytes_dropped +=
        (uint32_t)stack->remote_console_transmit_count;
    arwill_tcp_listener_reset(
        &stack->tcp_listener,
        0x41520000U + stack->remote_console_connections
    );
    stack->remote_console_receive_head = 0;
    stack->remote_console_receive_count = 0;
    stack->remote_console_transmit_head = 0;
    stack->remote_console_transmit_count = 0;
    stack->remote_console_peer_closed = 0;
    stack->tcp_pending.active = 0;
    stack->tcp_pending.payload_length = 0;
    stack->tcp_pending.retransmissions = 0;
    stack->tcp_time_wait_started_milliseconds = 0;
}

int arwill_ipv4_remote_console_running(const struct arwill_ipv4_stack *stack) {
    return stack != 0 && stack->remote_console_running;
}

int arwill_ipv4_remote_console_start(struct arwill_ipv4_stack *stack,
    uint16_t port) {
    if (stack == 0 || stack->network == 0 || port == 0U) {
        return 0;
    }
    if (arwill_ipv4_remote_console_connected(stack)) {
        arwill_ipv4_remote_console_close(stack);
    }
    arwill_tcp_listener_init(
        &stack->tcp_listener,
        port,
        0x41520000U + stack->remote_console_connections
    );
    stack->remote_console_receive_head = 0;
    stack->remote_console_receive_count = 0;
    stack->remote_console_transmit_head = 0;
    stack->remote_console_transmit_count = 0;
    stack->remote_console_peer_closed = 0;
    stack->tcp_pending.active = 0;
    stack->tcp_pending.payload_length = 0;
    stack->tcp_pending.retransmissions = 0;
    stack->remote_console_running = 1;
    return 1;
}

void arwill_ipv4_remote_console_stop(struct arwill_ipv4_stack *stack) {
    if (stack == 0) {
        return;
    }
    if (arwill_ipv4_remote_console_connected(stack)) {
        arwill_ipv4_remote_console_close(stack);
    }
    reset_remote_console(stack);
    stack->remote_console_running = 0;
}

static int remember_pending_segment(struct arwill_ipv4_stack *stack,
    const struct arwill_tcp_segment *segment, const uint8_t *payload,
    size_t payload_length) {
    if (stack == 0 || segment == 0
        || payload_length > arwill_tcp_pending_payload_capacity
        || (payload == 0 && payload_length != 0U)) {
        return 0;
    }

    stack->tcp_pending.segment = *segment;
    for (size_t index = 0; index < payload_length; index++) {
        stack->tcp_pending.payload[index] = payload[index];
    }
    stack->tcp_pending.payload_length = payload_length;
    stack->tcp_pending.expected_acknowledgement = segment->sequence
        + (uint32_t)payload_length;
    if ((segment->flags & arwill_tcp_flag_syn) != 0U
        || (segment->flags & arwill_tcp_flag_fin) != 0U) {
        stack->tcp_pending.expected_acknowledgement++;
    }
    stack->tcp_pending.sent_at_milliseconds =
        arwill_clock_monotonic_milliseconds(stack->clock);
    stack->tcp_pending.retransmissions = 0;
    stack->tcp_pending.active = 1;
    return 1;
}

static void acknowledge_pending_segment(struct arwill_ipv4_stack *stack,
    const struct arwill_tcp_segment *incoming) {
    if (stack->tcp_pending.active
        && (incoming->flags & arwill_tcp_flag_ack) != 0U
        && incoming->acknowledgement
            == stack->tcp_pending.expected_acknowledgement) {
        stack->tcp_pending.active = 0;
        stack->tcp_pending.payload_length = 0;
        stack->tcp_pending.retransmissions = 0;
    }
}

static void maintain_pending_segment(struct arwill_ipv4_stack *stack) {
    if (stack == 0 || !stack->tcp_pending.active) {
        return;
    }

    const uint64_t now = arwill_clock_monotonic_milliseconds(stack->clock);
    if (now - stack->tcp_pending.sent_at_milliseconds
        < arwill_tcp_retransmission_interval_ms) {
        return;
    }

    if (stack->tcp_pending.retransmissions >= arwill_tcp_max_retransmissions) {
        const int was_connected = arwill_ipv4_remote_console_connected(stack);
        stack->tcp_timeouts++;
        if (was_connected) {
            stack->remote_console_disconnects++;
        }
        reset_remote_console(stack);
        return;
    }

    if (!send_tcp_segment(stack, &stack->tcp_pending.segment,
            stack->tcp_pending.payload, stack->tcp_pending.payload_length)) {
        stack->remote_console_send_failures++;
    }
    stack->tcp_pending.sent_at_milliseconds = now;
    stack->tcp_pending.retransmissions++;
    stack->tcp_retransmissions++;
}

static void maintain_tcp_close(struct arwill_ipv4_stack *stack) {
    if (stack == 0 || stack->tcp_listener.state != arwill_tcp_state_time_wait) {
        return;
    }
    const uint64_t now = arwill_clock_monotonic_milliseconds(stack->clock);
    if (stack->tcp_time_wait_started_milliseconds == 0U) {
        stack->tcp_time_wait_started_milliseconds = now;
    }
    if (now - stack->tcp_time_wait_started_milliseconds >= arwill_tcp_time_wait_ms) {
        reset_remote_console(stack);
    }
}

static void remember_tcp_peer(struct arwill_ipv4_stack *stack, const uint8_t *frame) {
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        stack->tcp_peer_mac[index] = frame[6U + index];
    }
}

static void enqueue_remote_console(struct arwill_ipv4_stack *stack,
    const uint8_t *payload, size_t payload_length) {
    for (size_t index = 0; index < payload_length; index++) {
        if (stack->remote_console_receive_count
            >= arwill_remote_console_receive_capacity) {
            stack->remote_console_bytes_dropped++;
            continue;
        }
        const size_t tail = (stack->remote_console_receive_head
            + stack->remote_console_receive_count)
            % arwill_remote_console_receive_capacity;
        stack->remote_console_receive[tail] = payload[index];
        stack->remote_console_receive_count++;
        stack->remote_console_bytes_received++;
    }
}

static int flush_remote_console_transmit(struct arwill_ipv4_stack *stack) {
    if (!arwill_ipv4_remote_console_connected(stack) ||
        stack->tcp_pending.active ||
        stack->remote_console_transmit_count == 0U) {
        return 1;
    }

    uint8_t payload[arwill_tcp_pending_payload_capacity];
    size_t length = stack->remote_console_transmit_count;
    if (length > sizeof(payload)) {
        length = sizeof(payload);
    }
    for (size_t index = 0; index < length; index++) {
        payload[index] = stack->remote_console_transmit[
            (stack->remote_console_transmit_head + index) %
                arwill_remote_console_transmit_capacity
        ];
    }

    struct arwill_tcp_segment segment;
    for (size_t index = 0; index < 4U; index++) {
        segment.source_address[index] = stack->tcp_listener.local_address[index];
        segment.destination_address[index] = stack->tcp_listener.peer_address[index];
    }
    segment.source_port = stack->tcp_listener.port;
    segment.destination_port = stack->tcp_listener.peer_port;
    segment.sequence = stack->tcp_listener.sequence;
    segment.acknowledgement = stack->tcp_listener.acknowledgement;
    segment.flags = arwill_tcp_flag_ack | arwill_tcp_flag_psh;
    segment.payload_length = length;
    if (!send_tcp_segment(stack, &segment, payload, length) ||
        !remember_pending_segment(stack, &segment, payload, length)) {
        stack->remote_console_send_failures++;
        stack->tcp_pending.active = 0;
        return 0;
    }
    stack->tcp_listener.sequence += (uint32_t)length;
    stack->remote_console_bytes_sent += (uint32_t)length;
    stack->remote_console_transmit_head =
        (stack->remote_console_transmit_head + length) %
            arwill_remote_console_transmit_capacity;
    stack->remote_console_transmit_count -= length;
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
    if (!send_tcp_segment_to(stack, &reset, 0, 0U, destination_mac)) {
        return 0;
    }
    stack->tcp_rst_sent++;
    return 1;
}

int arwill_ipv4_poll_tcp(struct arwill_ipv4_stack *stack) {
    uint8_t frame[arwill_network_frame_capacity];
    size_t length = 0;
    if (stack == 0 || stack->network == 0) {
        return 0;
    }
    if (stack->remote_console_running) {
        maintain_pending_segment(stack);
        maintain_tcp_close(stack);
        (void)flush_remote_console_transmit(stack);
    }
    if (!arwill_network_poll_frame(stack->network, frame, sizeof(frame), &length)) {
        return 0;
    }
    if (length >= 42U && get16(frame, 12U) == 0x0806U && get16(frame, 20U) == 1U &&
        same_bytes(frame + 38U, stack->address, 4U)) {
        return send_arp_reply(stack, frame);
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

    if (!stack->remote_console_running ||
        incoming.destination_port != stack->tcp_listener.port) {
        stack->tcp_unknown_port_frames++;
        return send_reset_for_segment(stack, &incoming, frame + 6U);
    }
    if (!arwill_tcp_listener_matches(&stack->tcp_listener, &incoming)) {
        stack->tcp_tuple_mismatches++;
        return 0;
    }

    const enum arwill_tcp_state previous_state = stack->tcp_listener.state;
    const uint32_t previous_acknowledgement = stack->tcp_listener.acknowledgement;
    if (!arwill_tcp_listener_receive(&stack->tcp_listener, &incoming, &reply)) {
        return 0;
    }
    acknowledge_pending_segment(stack, &incoming);

    if (previous_state != arwill_tcp_state_listen &&
        stack->tcp_listener.state == arwill_tcp_state_listen &&
        (incoming.flags & arwill_tcp_flag_rst) != 0U) {
        stack->remote_console_disconnects++;
        reset_remote_console(stack);
        return 1;
    }

    if (reply.flags == (arwill_tcp_flag_syn | arwill_tcp_flag_ack)) {
        remember_tcp_peer(stack, frame);
        stack->tcp_syn_ack_sent++;
    }

    if (previous_state == arwill_tcp_state_syn_received
        && stack->tcp_listener.state == arwill_tcp_state_established) {
        stack->remote_console_connections++;
    }

    if (previous_state == arwill_tcp_state_established
        && incoming.sequence != previous_acknowledgement
        && reply.flags == arwill_tcp_flag_ack) {
        stack->tcp_duplicate_acks++;
    }

    if ((stack->tcp_listener.state == arwill_tcp_state_established ||
            stack->tcp_listener.state == arwill_tcp_state_close_wait)
        && payload_length != 0U
        && stack->tcp_listener.acknowledgement != previous_acknowledgement) {
        enqueue_remote_console(stack, payload, payload_length);
    }

    if ((incoming.flags & arwill_tcp_flag_fin) != 0U
        && stack->tcp_listener.acknowledgement != previous_acknowledgement) {
        stack->remote_console_peer_closed = 1;
    }

    if (stack->tcp_listener.state == arwill_tcp_state_time_wait &&
        previous_state != arwill_tcp_state_time_wait) {
        stack->tcp_time_wait_started_milliseconds =
            arwill_clock_monotonic_milliseconds(stack->clock);
    }

    if (!send_tcp_segment(stack, &reply, 0, 0U)) {
        return 0;
    }
    if (reply.flags == (arwill_tcp_flag_syn | arwill_tcp_flag_ack)
        && !remember_pending_segment(stack, &reply, 0, 0U)) {
        reset_remote_console(stack);
        return 0;
    }
    (void)flush_remote_console_transmit(stack);
    return 1;
}

int arwill_ipv4_remote_console_connected(const struct arwill_ipv4_stack *stack) {
    return stack != 0 && stack->remote_console_running &&
        arwill_tcp_listener_connected(&stack->tcp_listener);
}

int arwill_ipv4_remote_console_peer_closed(const struct arwill_ipv4_stack *stack) {
    return stack != 0 && stack->remote_console_peer_closed;
}

int arwill_ipv4_remote_console_read_byte(struct arwill_ipv4_stack *stack, uint8_t *byte) {
    if (stack == 0 || byte == 0 || stack->remote_console_receive_count == 0U) {
        return 0;
    }

    *byte = stack->remote_console_receive[stack->remote_console_receive_head];
    stack->remote_console_receive_head = (stack->remote_console_receive_head + 1U)
        % arwill_remote_console_receive_capacity;
    stack->remote_console_receive_count--;
    return 1;
}

int arwill_ipv4_remote_console_write(struct arwill_ipv4_stack *stack,
    const uint8_t *data, size_t length) {
    if (!arwill_ipv4_remote_console_connected(stack)
        || (data == 0 && length != 0U)) {
        return 0;
    }
    if (length > arwill_remote_console_transmit_capacity -
            stack->remote_console_transmit_count) {
        stack->remote_console_bytes_dropped += (uint32_t)length;
        return 0;
    }
    for (size_t index = 0; index < length; index++) {
        const size_t tail = (stack->remote_console_transmit_head +
            stack->remote_console_transmit_count) %
            arwill_remote_console_transmit_capacity;
        stack->remote_console_transmit[tail] = data[index];
        stack->remote_console_transmit_count++;
    }
    return flush_remote_console_transmit(stack);
}

void arwill_ipv4_remote_console_close(struct arwill_ipv4_stack *stack) {
    if (!arwill_ipv4_remote_console_connected(stack)) {
        return;
    }

    while ((stack->tcp_pending.active ||
            stack->remote_console_transmit_count != 0U)
        && arwill_ipv4_remote_console_connected(stack)) {
        (void)arwill_ipv4_poll_tcp(stack);
        if (stack->tcp_pending.active) {
            arwill_cpu_wait_for_interrupt();
        }
    }
    if (!arwill_ipv4_remote_console_connected(stack)) {
        return;
    }

    struct arwill_tcp_segment segment;
    if (!arwill_tcp_listener_begin_close(&stack->tcp_listener, &segment) ||
        !send_tcp_segment(stack, &segment, 0, 0U) ||
        !remember_pending_segment(stack, &segment, 0, 0U)) {
        stack->remote_console_send_failures++;
        reset_remote_console(stack);
        return;
    }
    stack->remote_console_disconnects++;
    const uint64_t started = arwill_clock_monotonic_milliseconds(stack->clock);
    while (stack->tcp_listener.state != arwill_tcp_state_listen &&
        arwill_clock_monotonic_milliseconds(stack->clock) - started <
            arwill_tcp_close_timeout_ms) {
        (void)arwill_ipv4_poll_tcp(stack);
        arwill_cpu_wait_for_interrupt();
    }
    reset_remote_console(stack);
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
