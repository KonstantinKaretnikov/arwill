#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <arwill/kernel/clock.h>
#include <arwill/kernel/cpu.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/tcp.h>

struct fake_network {
    uint8_t incoming[arwill_network_frame_capacity];
    size_t incoming_length;
    int incoming_ready;
    uint8_t outgoing[arwill_network_frame_capacity];
    size_t outgoing_length;
    unsigned send_count;
};

struct fake_clock {
    uint64_t milliseconds;
};

enum {
    test_remote_console_port = 23232
};

static const uint8_t guest_mac[arwill_network_mac_length] = {
    0x52U, 0x54U, 0x00U, 0x12U, 0x34U, 0x56U
};

static const uint8_t peer_mac[arwill_network_mac_length] = {
    0x52U, 0x54U, 0x00U, 0xaaU, 0xbbU, 0xccU
};

static void put16(uint8_t *buffer, size_t offset, uint16_t value) {
    buffer[offset] = (uint8_t)(value >> 8U);
    buffer[offset + 1U] = (uint8_t)value;
}

static uint16_t get16(const uint8_t *buffer, size_t offset) {
    return (uint16_t)(((uint16_t)buffer[offset] << 8U) | buffer[offset + 1U]);
}

static void put32(uint8_t *buffer, size_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)(value >> 24U);
    buffer[offset + 1U] = (uint8_t)(value >> 16U);
    buffer[offset + 2U] = (uint8_t)(value >> 8U);
    buffer[offset + 3U] = (uint8_t)value;
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

static int fake_send_frame(void *context, const uint8_t *frame, size_t length) {
    struct fake_network *network = (struct fake_network *)context;
    if (network == 0 || frame == 0 || length > sizeof(network->outgoing)) {
        return 0;
    }
    for (size_t index = 0; index < length; index++) {
        network->outgoing[index] = frame[index];
    }
    network->outgoing_length = length;
    network->send_count++;
    return 1;
}

static int fake_poll_frame(void *context, uint8_t *frame, size_t capacity,
    size_t *length) {
    struct fake_network *network = (struct fake_network *)context;
    if (network == 0 || frame == 0 || length == 0 || !network->incoming_ready
        || network->incoming_length > capacity) {
        return 0;
    }
    for (size_t index = 0; index < network->incoming_length; index++) {
        frame[index] = network->incoming[index];
    }
    *length = network->incoming_length;
    network->incoming_ready = 0;
    return 1;
}

static int fake_read_mac(void *context, uint8_t mac[arwill_network_mac_length]) {
    (void)context;
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        mac[index] = guest_mac[index];
    }
    return 1;
}

static uint64_t fake_milliseconds(void *context) {
    const struct fake_clock *clock = (const struct fake_clock *)context;
    return clock == 0 ? 0U : clock->milliseconds;
}

void arwill_cpu_wait_for_interrupt(void) {
}

static void queue_echo_request(struct fake_network *network) {
    uint8_t *frame = network->incoming;
    uint8_t *ip = frame + 14U;
    uint8_t *icmp = ip + 20U;
    const uint8_t payload[4] = { 0x61U, 0x72U, 0x77U, 0x69U };

    for (size_t index = 0; index < sizeof(network->incoming); index++) {
        frame[index] = 0U;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = guest_mac[index];
        frame[6U + index] = peer_mac[index];
    }
    put16(frame, 12U, 0x0800U);
    ip[0] = 0x45U;
    put16(ip, 2U, 32U);
    put16(ip, 4U, 0x1234U);
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 1U;
    ip[12] = 10U;
    ip[13] = 0U;
    ip[14] = 2U;
    ip[15] = 2U;
    ip[16] = 10U;
    ip[17] = 0U;
    ip[18] = 2U;
    ip[19] = 15U;
    put16(ip, 10U, checksum(ip, 20U));
    icmp[0] = 8U;
    put16(icmp, 4U, 0x4321U);
    put16(icmp, 6U, 7U);
    for (size_t index = 0; index < sizeof(payload); index++) {
        icmp[8U + index] = payload[index];
    }
    put16(icmp, 2U, checksum(icmp, 12U));
    network->incoming_length = 60U;
    network->incoming_ready = 1;
}

static void queue_segment_for_port(struct fake_network *network,
    uint16_t source_port, uint16_t destination_port,
    uint32_t sequence, uint32_t acknowledgement, uint8_t flags,
    const uint8_t *payload, size_t payload_length) {
    uint8_t *frame = network->incoming;
    uint8_t *ip = frame + 14U;
    uint8_t *tcp = ip + 20U;
    const size_t ip_length = 40U + payload_length;
    const size_t frame_length = 14U + ip_length;

    for (size_t index = 0; index < sizeof(network->incoming); index++) {
        frame[index] = 0;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = guest_mac[index];
        frame[6U + index] = peer_mac[index];
    }
    put16(frame, 12U, 0x0800U);
    ip[0] = 0x45U;
    put16(ip, 2U, (uint16_t)ip_length);
    ip[6] = 0x40U;
    ip[8] = 64U;
    ip[9] = 6U;
    ip[12] = 10U;
    ip[13] = 0U;
    ip[14] = 2U;
    ip[15] = 2U;
    ip[16] = 10U;
    ip[17] = 0U;
    ip[18] = 2U;
    ip[19] = 15U;
    put16(ip, 10U, checksum(ip, 20U));
    put16(tcp, 0U, source_port);
    put16(tcp, 2U, destination_port);
    put32(tcp, 4U, sequence);
    put32(tcp, 8U, acknowledgement);
    tcp[12] = 0x50U;
    tcp[13] = flags;
    put16(tcp, 14U, 4096U);
    for (size_t index = 0; index < payload_length; index++) {
        tcp[20U + index] = payload[index];
    }
    put16(tcp, 16U, tcp_checksum(ip, tcp, 20U + payload_length));
    network->incoming_length = frame_length < 60U ? 60U : frame_length;
    network->incoming_ready = 1;
}

static void queue_segment(struct fake_network *network, uint16_t source_port,
    uint32_t sequence, uint32_t acknowledgement, uint8_t flags,
    const uint8_t *payload, size_t payload_length) {
    queue_segment_for_port(network, source_port, test_remote_console_port,
        sequence, acknowledgement, flags, payload, payload_length);
}

static void change_queued_source_address(struct fake_network *network,
    uint8_t last_octet) {
    uint8_t *ip = network->incoming + 14U;
    uint8_t *tcp = ip + 20U;
    const size_t tcp_length = (size_t)get16(ip, 2U) - 20U;
    ip[15] = last_octet;
    put16(ip, 10U, 0U);
    put16(ip, 10U, checksum(ip, 20U));
    put16(tcp, 16U, 0U);
    put16(tcp, 16U, tcp_checksum(ip, tcp, tcp_length));
}

static void add_queued_maximum_segment_size(struct fake_network *network,
    uint16_t maximum_segment_size) {
    uint8_t *ip = network->incoming + 14U;
    uint8_t *tcp = ip + 20U;
    put16(ip, 2U, 44U);
    tcp[12] = 0x60U;
    tcp[20] = 2U;
    tcp[21] = 4U;
    put16(tcp, 22U, maximum_segment_size);
    put16(ip, 10U, 0U);
    put16(ip, 10U, checksum(ip, 20U));
    put16(tcp, 16U, 0U);
    put16(tcp, 16U, tcp_checksum(ip, tcp, 24U));
}

static int expect(int condition, const char *message) {
    if (condition) {
        return 1;
    }
    fprintf(stderr, "ipv4 test failed: %s\n", message);
    return 0;
}

int main(void) {
    struct fake_network fake = { 0 };
    struct fake_clock time = { 0 };
    const struct arwill_network_device network = {
        .name = "fake",
        .context = &fake,
        .send_frame = fake_send_frame,
        .poll_frame = fake_poll_frame,
        .read_mac = fake_read_mac,
    };
    const struct arwill_clock clock = {
        .name = "fake",
        .context = &time,
        .monotonic_milliseconds = fake_milliseconds,
    };
    struct arwill_ipv4_stack stack;
    const uint16_t peer_port = 42000U;
    const uint32_t peer_initial_sequence = 1000U;

    if (!expect(arwill_ipv4_init(
            &stack, &network, &clock, test_remote_console_port
        ), "stack initialization")) {
        return 1;
    }

    arwill_tcp_stream_stop(&stack.stream);
    queue_echo_request(&fake);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "ICMP echo request accepted")
        || !expect(fake.send_count == 1U, "ICMP echo reply sent")
        || !expect(fake.outgoing_length == 60U, "ICMP reply padded")
        || !expect(get16(fake.outgoing, 12U) == 0x0800U,
            "ICMP reply Ethernet type")
        || !expect(fake.outgoing[23] == 1U, "ICMP reply IPv4 protocol")
        || !expect(fake.outgoing[34] == 0U && fake.outgoing[35] == 0U,
            "ICMP echo reply type and code")
        || !expect(get16(fake.outgoing, 38U) == 0x4321U,
            "ICMP identifier preserved")
        || !expect(get16(fake.outgoing, 40U) == 7U,
            "ICMP sequence preserved")
        || !expect(fake.outgoing[42] == 0x61U && fake.outgoing[43] == 0x72U &&
            fake.outgoing[44] == 0x77U && fake.outgoing[45] == 0x69U,
            "ICMP payload preserved")
        || !expect(checksum(fake.outgoing + 14U, 20U) == 0U,
            "ICMP reply IPv4 checksum")
        || !expect(checksum(fake.outgoing + 34U, 12U) == 0U,
            "ICMP reply checksum")
        || !expect(stack.icmp_echo_requests_received == 1U,
            "ICMP request counted")
        || !expect(stack.icmp_echo_replies_sent == 1U,
            "ICMP reply counted")) {
        return 1;
    }

    queue_echo_request(&fake);
    fake.incoming[44] ^= 1U;
    if (!expect(!arwill_ipv4_poll_tcp(&stack),
            "bad ICMP checksum rejected")
        || !expect(fake.send_count == 1U,
            "bad ICMP request produced no reply")
        || !expect(stack.icmp_checksum_drops == 1U,
            "bad ICMP checksum counted")) {
        return 1;
    }
    fake.send_count = 0U;
    fake.outgoing_length = 0U;
    if (!expect(arwill_tcp_stream_listen(
            &stack.stream, test_remote_console_port, 0x41520000U
        ), "remote console restarted after ICMP test")) {
        return 1;
    }

    queue_segment_for_port(&fake, peer_port, 24000U, peer_initial_sequence, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "closed port SYN handled")
        || !expect(stack.tcp_unknown_port_frames == 1U,
            "closed port counted")
        || !expect(stack.tcp_rst_sent == 1U, "closed port RST counted")
        || !expect((fake.outgoing[47] &
            (arwill_tcp_flag_rst | arwill_tcp_flag_ack)) ==
            (arwill_tcp_flag_rst | arwill_tcp_flag_ack),
            "closed port produced RST-ACK")) {
        return 1;
    }
    fake.send_count = 0U;
    fake.outgoing_length = 0U;

    queue_segment(&fake, peer_port, peer_initial_sequence, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    fake.incoming[50] ^= 1U;
    if (!expect(!arwill_ipv4_poll_tcp(&stack), "bad TCP checksum rejected")
        || !expect(stack.tcp_checksum_drops == 1U, "checksum drop counted")
        || !expect(fake.send_count == 0U, "bad segment produced no reply")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    fake.incoming[24] ^= 1U;
    if (!expect(!arwill_ipv4_poll_tcp(&stack), "bad IPv4 checksum rejected")
        || !expect(stack.tcp_checksum_drops == 2U, "IPv4 checksum drop counted")
        || !expect(fake.send_count == 0U, "bad IPv4 packet produced no reply")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    add_queued_maximum_segment_size(&fake, 128U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "SYN accepted")
        || !expect(stack.stream.listener.state == arwill_tcp_state_syn_received,
            "listener entered syn-received")
        || !expect(stack.tcp_pending_count == 1U, "SYN-ACK retained")
        || !expect(fake.send_count == 1U, "SYN-ACK sent")
        || !expect(stack.stream.listener.peer_maximum_segment_size == 128U,
            "peer MSS parsed")
        || !expect((fake.outgoing[46] >> 4U) == 6U,
            "SYN-ACK contains TCP options")
        || !expect(fake.outgoing[54] == 2U && fake.outgoing[55] == 4U &&
            get16(fake.outgoing, 56U) == arwill_tcp_local_maximum_segment_size,
            "local MSS advertised")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "repeated SYN accepted")
        || !expect(fake.send_count == 2U, "repeated SYN produced SYN-ACK")) {
        return 1;
    }

    time.milliseconds += arwill_tcp_retransmission_interval_ms - 1U;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == 2U, "SYN-ACK not retransmitted early")) {
        return 1;
    }
    time.milliseconds += 1U;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == 3U, "SYN-ACK retransmitted")
        || !expect(stack.tcp_retransmissions == 1U, "retransmission counted")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.stream.listener.sequence + 1U, arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "handshake ACK accepted")
        || !expect(arwill_tcp_stream_connected(&stack.stream),
            "listener established")
        || !expect(stack.tcp_pending_count == 0U, "SYN-ACK acknowledged")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.stream.listener.sequence, arwill_tcp_flag_ack, 0, 0U);
    change_queued_source_address(&fake, 3U);
    const unsigned sends_before_tuple_mismatch = fake.send_count;
    if (!expect(!arwill_ipv4_poll_tcp(&stack), "wrong peer tuple rejected")
        || !expect(stack.tcp_tuple_mismatches == 1U,
            "wrong peer tuple counted")
        || !expect(fake.send_count == sends_before_tuple_mismatch,
            "wrong peer tuple produced no reply")
        || !expect(arwill_tcp_stream_connected(&stack.stream),
            "wrong peer did not disturb connection")) {
        return 1;
    }

    const uint8_t input = (uint8_t)'x';
    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "payload accepted")) {
        return 1;
    }
    uint8_t received = 0;
    if (!expect(arwill_tcp_stream_read(&stack.stream, &received, 1U) == 1U,
            "payload queued")
        || !expect(received == input, "queued payload preserved")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "duplicate payload acknowledged")
        || !expect(stack.tcp_duplicate_acks == 1U, "duplicate ACK counted")
        || !expect(!arwill_tcp_stream_read(&stack.stream, &received, 1U),
            "duplicate payload not queued")) {
        return 1;
    }

    const uint8_t output[2] = { (uint8_t)'o', (uint8_t)'k' };
    if (!expect(arwill_tcp_stream_write(
            &stack.stream, output, sizeof(output)) == sizeof(output),
            "console output queued")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.tcp_pending_count == 1U, "console output retained")) {
        return 1;
    }
    const unsigned sends_before_retry = fake.send_count;
    time.milliseconds += arwill_tcp_retransmission_interval_ms;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == sends_before_retry + 1U, "output retransmitted")) {
        return 1;
    }

    queue_segment(&fake, peer_port, stack.stream.listener.acknowledgement,
        stack.stream.listener.sequence, arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "output ACK accepted")
        || !expect(stack.tcp_pending_count == 0U,
            "output ACK cleared pending segment")) {
        return 1;
    }

    uint8_t multi_output[300];
    for (size_t index = 0; index < sizeof(multi_output); index++) {
        multi_output[index] = (uint8_t)('a' + index % 26U);
    }
    const unsigned sends_before_multi = fake.send_count;
    if (!expect(arwill_tcp_stream_write(
            &stack.stream, multi_output, sizeof(multi_output)) ==
                sizeof(multi_output),
            "multi-segment output queued")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.tcp_pending_count == 3U,
            "three output segments retained")
        || !expect(fake.send_count == sends_before_multi + 3U,
            "three output segments transmitted")) {
        return 1;
    }
    queue_segment(&fake, peer_port, stack.stream.listener.acknowledgement,
        stack.stream.listener.sequence, arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "cumulative ACK accepted")
        || !expect(stack.tcp_pending_count == 0U,
            "cumulative ACK cleared send flight")
        || !expect(stack.tcp_retransmission_timeout_ms >=
            arwill_tcp_retransmission_minimum_ms,
            "adaptive RTO respects minimum")
        || !expect(stack.tcp_retransmission_timeout_ms <=
            arwill_tcp_retransmission_maximum_ms,
            "adaptive RTO respects maximum")) {
        return 1;
    }

    uint8_t receive_fill[arwill_tcp_stream_receive_capacity];
    for (size_t index = 0; index < sizeof(receive_fill); index++) {
        receive_fill[index] = (uint8_t)'w';
    }
    queue_segment(&fake, peer_port, stack.stream.listener.acknowledgement,
        stack.stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        receive_fill, sizeof(receive_fill));
    if (!expect(arwill_ipv4_poll_tcp(&stack), "receive window filled")
        || !expect(stack.stream.receive_count ==
            arwill_tcp_stream_receive_capacity, "receive ring is full")
        || !expect(stack.tcp_last_advertised_window == 0U,
            "zero receive window advertised")) {
        return 1;
    }
    const uint32_t acknowledgement_before_window_drop =
        stack.stream.listener.acknowledgement;
    queue_segment(&fake, peer_port, stack.stream.listener.acknowledgement,
        stack.stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "closed receive window handled")
        || !expect(stack.tcp_receive_window_drops == 1U,
            "closed receive window drop counted")
        || !expect(stack.stream.listener.acknowledgement ==
            acknowledgement_before_window_drop,
            "unretained byte was not acknowledged")) {
        return 1;
    }
    const unsigned sends_before_window_update = fake.send_count;
    if (!expect(arwill_tcp_stream_read(&stack.stream, &received, 1U) == 1U,
            "one byte freed from zero window")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.tcp_window_updates == 1U, "window update counted")
        || !expect(fake.send_count == sends_before_window_update + 1U,
            "window update transmitted")
        || !expect(stack.tcp_last_advertised_window == 1U,
            "reopened receive window advertised")) {
        return 1;
    }

    if (!expect(arwill_tcp_stream_write(&stack.stream, output, 1U) == 1U,
            "timeout probe queued")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    for (unsigned attempt = 0; attempt <= arwill_tcp_max_retransmissions; attempt++) {
        const struct arwill_tcp_pending_segment *pending =
            &stack.tcp_pending[stack.tcp_pending_head];
        time.milliseconds += pending->retransmission_timeout_ms;
        (void)arwill_ipv4_poll_tcp(&stack);
    }
    if (!expect(stack.tcp_timeouts == 1U, "retry exhaustion counted")
        || !expect(stack.stream.listener.state == arwill_tcp_state_listen,
            "timeout returned listener to listen")
        || !expect(stack.tcp_pending_count == 0U,
            "timeout cleared pending segment")) {
        return 1;
    }

    stack.stream.listener.state = arwill_tcp_state_established;
    stack.stream.close_requested = 1;
    stack.stream.listener.peer_port = (uint16_t)(peer_port - 1U);
    for (size_t index = 0; index < 4U; index++) {
        stack.stream.listener.local_address[index] = stack.address[index];
    }
    stack.stream.listener.peer_address[0] = 10U;
    stack.stream.listener.peer_address[1] = 0U;
    stack.stream.listener.peer_address[2] = 2U;
    stack.stream.listener.peer_address[3] = 2U;
    queue_segment(&fake, peer_port, peer_initial_sequence + 100U, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "new tuple SYN replaces closing connection")
        || !expect(stack.stream.listener.state == arwill_tcp_state_syn_received,
            "listener accepts reconnect during old tuple close")) {
        return 1;
    }
    stack.tcp_pending_count = 0;
    stack.stream.listener.state = arwill_tcp_state_fin_wait_2;
    stack.tcp_close_started_milliseconds = time.milliseconds;
    time.milliseconds += arwill_tcp_close_timeout_ms;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.stream.listener.state == arwill_tcp_state_listen,
            "nonblocking close timeout restores listener")) {
        return 1;
    }

    if (!expect(arwill_tcp_sequence_before(UINT32_MAX, 0U),
            "sequence comparison wraps before zero")
        || !expect(arwill_tcp_sequence_after(0U, UINT32_MAX),
            "sequence comparison wraps after max")) {
        return 1;
    }

    struct arwill_tcp_listener close_listener;
    struct arwill_tcp_segment close_incoming = { 0 };
    struct arwill_tcp_segment close_reply;
    struct arwill_tcp_segment close_fin;
    close_incoming.source_address[0] = 10U;
    close_incoming.source_address[2] = 2U;
    close_incoming.source_address[3] = 2U;
    close_incoming.destination_address[0] = 10U;
    close_incoming.destination_address[2] = 2U;
    close_incoming.destination_address[3] = 15U;
    close_incoming.source_port = peer_port;
    close_incoming.destination_port = test_remote_console_port;
    close_incoming.sequence = peer_initial_sequence;
    close_incoming.flags = arwill_tcp_flag_syn;
    arwill_tcp_listener_init(&close_listener, test_remote_console_port, UINT32_MAX);
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "close test SYN accepted")) {
        return 1;
    }
    close_incoming.sequence++;
    close_incoming.acknowledgement = 0U;
    close_incoming.flags = arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "wraparound handshake ACK accepted")
        || !expect(close_listener.state == arwill_tcp_state_established,
            "wraparound listener established")
        || !expect(close_listener.sequence == 0U,
            "local sequence wrapped to zero")
        || !expect(arwill_tcp_listener_begin_close(
            &close_listener, &close_fin), "active close began")
        || !expect(close_listener.state == arwill_tcp_state_fin_wait_1,
            "active close entered fin-wait-1")) {
        return 1;
    }
    close_incoming.acknowledgement = close_listener.sequence;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "FIN acknowledgement accepted")
        || !expect(close_listener.state == arwill_tcp_state_fin_wait_2,
            "active close entered fin-wait-2")) {
        return 1;
    }
    close_incoming.sequence = close_listener.acknowledgement;
    close_incoming.flags = arwill_tcp_flag_fin | arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "peer FIN accepted after active close")
        || !expect(close_listener.state == arwill_tcp_state_time_wait,
            "active close entered time-wait")
        || !expect(close_reply.flags == arwill_tcp_flag_ack,
            "peer FIN acknowledged")) {
        return 1;
    }

    arwill_tcp_listener_init(&close_listener, test_remote_console_port, 2000U);
    close_incoming.sequence = peer_initial_sequence;
    close_incoming.acknowledgement = 0U;
    close_incoming.flags = arwill_tcp_flag_syn;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "passive close test SYN accepted")) {
        return 1;
    }
    close_incoming.sequence++;
    close_incoming.acknowledgement = close_listener.sequence + 1U;
    close_incoming.flags = arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "passive close handshake ACK accepted")) {
        return 1;
    }
    close_incoming.acknowledgement = close_listener.sequence;
    close_incoming.flags = arwill_tcp_flag_fin | arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "peer initiated FIN accepted")
        || !expect(close_listener.state == arwill_tcp_state_close_wait,
            "peer FIN entered close-wait")
        || !expect(arwill_tcp_listener_begin_close(
            &close_listener, &close_fin), "passive close FIN began")
        || !expect(close_listener.state == arwill_tcp_state_last_ack,
            "passive close entered last-ack")) {
        return 1;
    }
    close_incoming.sequence = close_listener.acknowledgement;
    close_incoming.acknowledgement = close_listener.sequence;
    close_incoming.flags = arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "passive close final ACK accepted")
        || !expect(close_listener.state == arwill_tcp_state_listen,
            "passive close returned to listen")) {
        return 1;
    }

    struct arwill_tcp_stream contract_stream;
    arwill_tcp_stream_init(&contract_stream);
    if (!expect(arwill_tcp_stream_listen(
            &contract_stream, 24000U, 3000U), "stream contract listens")) {
        return 1;
    }
    contract_stream.listener.state = arwill_tcp_state_established;
    if (!expect(arwill_tcp_stream_write(
            &contract_stream, output, sizeof(output)) == sizeof(output),
            "stream write queues complete buffer")
        || !expect(contract_stream.transmit_count == sizeof(output),
            "stream write is nonblocking")
        || !expect(arwill_tcp_stream_write(
            &contract_stream, output,
            arwill_tcp_stream_transmit_capacity + 1U) == 0U,
            "stream rejects oversized write")
        || !expect(contract_stream.bytes_dropped ==
            arwill_tcp_stream_transmit_capacity + 1U,
            "stream counts rejected bytes")) {
        return 1;
    }
    arwill_tcp_stream_close(&contract_stream);
    if (!expect(contract_stream.close_requested,
            "stream close is a nonblocking request")) {
        return 1;
    }

    puts("IPv4/ICMP/TCP host test passed");
    return 0;
}
