#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <arwill/kernel/awp_network.h>
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

static uint16_t udp_checksum(const uint8_t *ip, const uint8_t *udp, size_t length) {
    uint32_t sum = 17U + (uint32_t)length;
    sum += get16(ip, 12U);
    sum += get16(ip, 14U);
    sum += get16(ip, 16U);
    sum += get16(ip, 18U);
    for (size_t index = 0; index + 1U < length; index += 2U) {
        sum += get16(udp, index);
    }
    if ((length & 1U) != 0U) {
        sum += (uint32_t)udp[length - 1U] << 8U;
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

static void queue_arp_reply(struct fake_network *network,
    const uint8_t sender_address[4]) {
    uint8_t *frame = network->incoming;

    for (size_t index = 0; index < sizeof(network->incoming); index++) {
        frame[index] = 0U;
    }
    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        frame[index] = guest_mac[index];
        frame[6U + index] = peer_mac[index];
        frame[22U + index] = peer_mac[index];
        frame[32U + index] = guest_mac[index];
    }
    put16(frame, 12U, 0x0806U);
    put16(frame, 14U, 1U);
    put16(frame, 16U, 0x0800U);
    frame[18] = 6U;
    frame[19] = 4U;
    put16(frame, 20U, 2U);
    for (size_t index = 0; index < 4U; index++) {
        frame[28U + index] = sender_address[index];
    }
    frame[38] = 10U;
    frame[39] = 0U;
    frame[40] = 2U;
    frame[41] = 15U;
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

static void queue_udp_datagram(struct fake_network *network,
    const uint8_t source_address[4], uint16_t source_port,
    uint16_t destination_port, const uint8_t *payload, size_t payload_length) {
    uint8_t *frame = network->incoming;
    uint8_t *ip = frame + 14U;
    uint8_t *udp = ip + 20U;
    const size_t udp_length = 8U + payload_length;
    const size_t ip_length = 20U + udp_length;
    const size_t frame_length = 14U + ip_length;

    for (size_t index = 0; index < sizeof(network->incoming); index++) {
        frame[index] = 0U;
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
    ip[9] = 17U;
    for (size_t index = 0; index < 4U; index++) {
        ip[12U + index] = source_address[index];
    }
    ip[16] = 10U;
    ip[17] = 0U;
    ip[18] = 2U;
    ip[19] = 15U;
    put16(ip, 10U, checksum(ip, 20U));
    put16(udp, 0U, source_port);
    put16(udp, 2U, destination_port);
    put16(udp, 4U, (uint16_t)udp_length);
    for (size_t index = 0; index < payload_length; index++) {
        udp[8U + index] = payload[index];
    }
    put16(udp, 6U, udp_checksum(ip, udp, udp_length));
    network->incoming_length = frame_length < 60U ? 60U : frame_length;
    network->incoming_ready = 1;
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

static int text_equals(const char *left, const char *right) {
    size_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        index++;
    }
    return left[index] == right[index];
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

    struct arwill_tcp_endpoint_snapshot endpoint_snapshot;
    if (!expect(arwill_ipv4_tcp_endpoint_snapshot(
            &stack, 0U, &endpoint_snapshot), "remote endpoint snapshot")
        || !expect(endpoint_snapshot.allocated && endpoint_snapshot.listening,
            "remote snapshot reports allocation")
        || !expect(text_equals(endpoint_snapshot.owner, "remote-console") &&
            text_equals(endpoint_snapshot.state, "listen"),
            "remote snapshot names owner and state")
        || !expect(endpoint_snapshot.local_port == test_remote_console_port,
            "remote snapshot reports local port")) {
        return 1;
    }

    arwill_tcp_stream_stop(&stack.endpoints[0].stream);
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
            &stack.endpoints[0].stream, test_remote_console_port, 0x41520000U
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
        || !expect(stack.endpoints[0].stream.listener.state == arwill_tcp_state_syn_received,
            "listener entered syn-received")
        || !expect(stack.endpoints[0].tcp_pending_count == 1U, "SYN-ACK retained")
        || !expect(fake.send_count == 1U, "SYN-ACK sent")
        || !expect(stack.endpoints[0].stream.listener.peer_maximum_segment_size == 128U,
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
        stack.endpoints[0].stream.listener.sequence + 1U, arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "handshake ACK accepted")
        || !expect(arwill_tcp_stream_connected(&stack.endpoints[0].stream),
            "listener established")
        || !expect(stack.endpoints[0].tcp_pending_count == 0U, "SYN-ACK acknowledged")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack, 0, 0U);
    change_queued_source_address(&fake, 3U);
    const unsigned sends_before_tuple_mismatch = fake.send_count;
    if (!expect(!arwill_ipv4_poll_tcp(&stack), "wrong peer tuple rejected")
        || !expect(stack.tcp_tuple_mismatches == 1U,
            "wrong peer tuple counted")
        || !expect(fake.send_count == sends_before_tuple_mismatch,
            "wrong peer tuple produced no reply")
        || !expect(arwill_tcp_stream_connected(&stack.endpoints[0].stream),
            "wrong peer did not disturb connection")) {
        return 1;
    }

    const uint8_t input = (uint8_t)'x';
    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "payload accepted")) {
        return 1;
    }
    uint8_t received = 0;
    if (!expect(arwill_tcp_stream_read(&stack.endpoints[0].stream, &received, 1U) == 1U,
            "payload queued")
        || !expect(received == input, "queued payload preserved")) {
        return 1;
    }

    queue_segment(&fake, peer_port, peer_initial_sequence + 1U,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "duplicate payload acknowledged")
        || !expect(stack.tcp_duplicate_acks == 1U, "duplicate ACK counted")
        || !expect(!arwill_tcp_stream_read(&stack.endpoints[0].stream, &received, 1U),
            "duplicate payload not queued")) {
        return 1;
    }

    const uint32_t expected_peer_sequence =
        stack.endpoints[0].stream.listener.acknowledgement;
    queue_segment(&fake, peer_port, expected_peer_sequence + 1U,
        stack.endpoints[0].stream.listener.sequence,
        arwill_tcp_flag_ack | arwill_tcp_flag_psh, &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "reordered payload acknowledged without acceptance")
        || !expect(stack.endpoints[0].stream.listener.acknowledgement ==
            expected_peer_sequence,
            "reordered payload did not advance receive sequence")
        || !expect(!arwill_tcp_stream_read(
            &stack.endpoints[0].stream, &received, 1U),
            "reordered payload was not queued")) {
        return 1;
    }

    const uint8_t output[2] = { (uint8_t)'o', (uint8_t)'k' };
    if (!expect(arwill_tcp_stream_write(
            &stack.endpoints[0].stream, output, sizeof(output)) == sizeof(output),
            "console output queued")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.endpoints[0].tcp_pending_count == 1U, "console output retained")) {
        return 1;
    }
    const unsigned sends_before_retry = fake.send_count;
    time.milliseconds += arwill_tcp_retransmission_interval_ms;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == sends_before_retry + 1U, "output retransmitted")) {
        return 1;
    }

    queue_segment(&fake, peer_port, stack.endpoints[0].stream.listener.acknowledgement,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "output ACK accepted")
        || !expect(stack.endpoints[0].tcp_pending_count == 0U,
            "output ACK cleared pending segment")) {
        return 1;
    }

    uint8_t multi_output[300];
    for (size_t index = 0; index < sizeof(multi_output); index++) {
        multi_output[index] = (uint8_t)('a' + index % 26U);
    }
    const unsigned sends_before_multi = fake.send_count;
    if (!expect(arwill_tcp_stream_write(
            &stack.endpoints[0].stream, multi_output, sizeof(multi_output)) ==
                sizeof(multi_output),
            "multi-segment output queued")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.endpoints[0].tcp_pending_count == 3U,
            "three output segments retained")
        || !expect(fake.send_count == sends_before_multi + 3U,
            "three output segments transmitted")) {
        return 1;
    }
    queue_segment(&fake, peer_port, stack.endpoints[0].stream.listener.acknowledgement,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "cumulative ACK accepted")
        || !expect(stack.endpoints[0].tcp_pending_count == 0U,
            "cumulative ACK cleared send flight")
        || !expect(stack.endpoints[0].tcp_retransmission_timeout_ms >=
            arwill_tcp_retransmission_minimum_ms,
            "adaptive RTO respects minimum")
        || !expect(stack.endpoints[0].tcp_retransmission_timeout_ms <=
            arwill_tcp_retransmission_maximum_ms,
            "adaptive RTO respects maximum")) {
        return 1;
    }

    uint8_t receive_fill[arwill_tcp_stream_receive_capacity];
    for (size_t index = 0; index < sizeof(receive_fill); index++) {
        receive_fill[index] = (uint8_t)'w';
    }
    queue_segment(&fake, peer_port, stack.endpoints[0].stream.listener.acknowledgement,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        receive_fill, sizeof(receive_fill));
    if (!expect(arwill_ipv4_poll_tcp(&stack), "receive window filled")
        || !expect(stack.endpoints[0].stream.receive_count ==
            arwill_tcp_stream_receive_capacity, "receive ring is full")
        || !expect(stack.endpoints[0].tcp_last_advertised_window == 0U,
            "zero receive window advertised")) {
        return 1;
    }
    const uint32_t acknowledgement_before_window_drop =
        stack.endpoints[0].stream.listener.acknowledgement;
    queue_segment(&fake, peer_port, stack.endpoints[0].stream.listener.acknowledgement,
        stack.endpoints[0].stream.listener.sequence, arwill_tcp_flag_ack | arwill_tcp_flag_psh,
        &input, 1U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "closed receive window handled")
        || !expect(stack.tcp_receive_window_drops == 1U,
            "closed receive window drop counted")
        || !expect(stack.endpoints[0].stream.listener.acknowledgement ==
            acknowledgement_before_window_drop,
            "unretained byte was not acknowledged")) {
        return 1;
    }
    const unsigned sends_before_window_update = fake.send_count;
    if (!expect(arwill_tcp_stream_read(&stack.endpoints[0].stream, &received, 1U) == 1U,
            "one byte freed from zero window")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.tcp_window_updates == 1U, "window update counted")
        || !expect(fake.send_count == sends_before_window_update + 1U,
            "window update transmitted")
        || !expect(stack.endpoints[0].tcp_last_advertised_window == 1U,
            "reopened receive window advertised")) {
        return 1;
    }

    if (!expect(arwill_tcp_stream_write(&stack.endpoints[0].stream, output, 1U) == 1U,
            "timeout probe queued")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    for (unsigned attempt = 0; attempt <= arwill_tcp_max_retransmissions; attempt++) {
        const struct arwill_tcp_pending_segment *pending =
            &stack.endpoints[0].tcp_pending[stack.endpoints[0].tcp_pending_head];
        time.milliseconds += pending->retransmission_timeout_ms;
        (void)arwill_ipv4_poll_tcp(&stack);
    }
    if (!expect(stack.tcp_timeouts == 1U, "retry exhaustion counted")
        || !expect(stack.endpoints[0].stream.listener.state == arwill_tcp_state_listen,
            "timeout returned listener to listen")
        || !expect(stack.endpoints[0].tcp_pending_count == 0U,
            "timeout cleared pending segment")) {
        return 1;
    }

    stack.endpoints[0].stream.listener.state = arwill_tcp_state_established;
    stack.endpoints[0].stream.close_requested = 1;
    stack.endpoints[0].stream.listener.peer_port = (uint16_t)(peer_port - 1U);
    for (size_t index = 0; index < 4U; index++) {
        stack.endpoints[0].stream.listener.local_address[index] = stack.address[index];
    }
    stack.endpoints[0].stream.listener.peer_address[0] = 10U;
    stack.endpoints[0].stream.listener.peer_address[1] = 0U;
    stack.endpoints[0].stream.listener.peer_address[2] = 2U;
    stack.endpoints[0].stream.listener.peer_address[3] = 2U;
    queue_segment(&fake, peer_port, peer_initial_sequence + 100U, 0U,
        arwill_tcp_flag_syn, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "new tuple SYN replaces closing connection")
        || !expect(stack.endpoints[0].stream.listener.state == arwill_tcp_state_syn_received,
            "listener accepts reconnect during old tuple close")) {
        return 1;
    }
    stack.endpoints[0].tcp_pending_count = 0;
    stack.endpoints[0].stream.listener.state = arwill_tcp_state_fin_wait_2;
    stack.endpoints[0].tcp_close_started_milliseconds = time.milliseconds;
    time.milliseconds += arwill_tcp_close_timeout_ms;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(stack.endpoints[0].stream.listener.state == arwill_tcp_state_listen,
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

    arwill_tcp_listener_init(&close_listener, test_remote_console_port, 4000U);
    close_incoming.sequence = peer_initial_sequence;
    close_incoming.acknowledgement = 0U;
    close_incoming.flags = arwill_tcp_flag_syn;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "RST test SYN accepted")) {
        return 1;
    }
    close_incoming.sequence++;
    close_incoming.acknowledgement = close_listener.sequence + 1U;
    close_incoming.flags = arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "RST test connection established")) {
        return 1;
    }
    close_incoming.acknowledgement = close_listener.sequence;
    close_incoming.flags = arwill_tcp_flag_rst | arwill_tcp_flag_ack;
    if (!expect(arwill_tcp_listener_receive(
            &close_listener, &close_incoming, &close_reply),
            "matching RST accepted")
        || !expect(close_listener.state == arwill_tcp_state_listen,
            "matching RST restores listener")) {
        return 1;
    }

    struct arwill_tcp_stream contract_stream;
    arwill_tcp_stream_init(&contract_stream);
    if (!expect(arwill_tcp_stream_listen(
            &contract_stream, 24000U, 3000U), "stream contract listens")) {
        return 1;
    }
    contract_stream.listener.state = arwill_tcp_state_established;
    contract_stream.connections = 1U;
    if (!expect(arwill_tcp_stream_write(
            &contract_stream, output, sizeof(output)) == sizeof(output),
            "stream write queues complete buffer")
        || !expect(arwill_tcp_stream_connection_seen(&contract_stream),
            "stream reports prior connection")
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
    contract_stream.listener.state = arwill_tcp_state_listen;
    contract_stream.close_requested = 0;
    if (!expect(arwill_tcp_stream_close_complete(&contract_stream),
            "stream reports listener close baseline")) {
        return 1;
    }

    struct arwill_tcp_stream *first = arwill_ipv4_tcp_open(&stack);
    struct arwill_tcp_stream *second = arwill_ipv4_tcp_open(&stack);
    struct arwill_tcp_stream *third = arwill_ipv4_tcp_open(&stack);
    if (!expect(first != 0 && second != 0 && third != 0,
            "three application endpoints allocated")
        || !expect(arwill_ipv4_tcp_open(&stack) == 0,
            "four-endpoint table is bounded")
        || !expect(arwill_ipv4_tcp_listen(&stack, first, 24000U),
            "first application endpoint listens")
        || !expect(!arwill_ipv4_tcp_listen(&stack, second, 24000U),
            "duplicate local port rejected")
        || !expect(arwill_ipv4_tcp_listen(&stack, second, 25000U),
            "second application endpoint listens")) {
        return 1;
    }

    const uint16_t first_peer_port = 41000U;
    const uint16_t second_peer_port = 42000U;
    const uint32_t first_peer_sequence = 7000U;
    const uint32_t second_peer_sequence = 8000U;
    queue_segment_for_port(&fake, first_peer_port, 24000U,
        first_peer_sequence, 0U, arwill_tcp_flag_syn, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "first endpoint SYN accepted")
        || !expect(first->listener.state == arwill_tcp_state_syn_received,
            "first endpoint owns its handshake")
        || !expect(second->listener.state == arwill_tcp_state_listen,
            "second endpoint remains independent")) {
        return 1;
    }
    queue_segment_for_port(&fake, second_peer_port, 25000U,
        second_peer_sequence, 0U, arwill_tcp_flag_syn, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "second endpoint SYN accepted")
        || !expect(second->listener.state == arwill_tcp_state_syn_received,
            "second endpoint owns its handshake")) {
        return 1;
    }
    queue_segment_for_port(&fake, first_peer_port, 24000U,
        first_peer_sequence + 1U, first->listener.sequence + 1U,
        arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "first endpoint established")) {
        return 1;
    }
    queue_segment_for_port(&fake, second_peer_port, 25000U,
        second_peer_sequence + 1U, second->listener.sequence + 1U,
        arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack), "second endpoint established")
        || !expect(arwill_tcp_stream_connected(first) &&
            arwill_tcp_stream_connected(second),
            "two ports are connected simultaneously")) {
        return 1;
    }
    const unsigned sends_before_two_streams = fake.send_count;
    if (!expect(arwill_tcp_stream_write(first, output, 1U) == 1U,
            "first endpoint queues output")
        || !expect(arwill_tcp_stream_write(second, output + 1U, 1U) == 1U,
            "second endpoint queues output")) {
        return 1;
    }
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == sends_before_two_streams + 2U,
            "two endpoint flights transmit independently")
        || !expect(stack.endpoints[1].tcp_pending_count == 1U &&
            stack.endpoints[2].tcp_pending_count == 1U,
            "two endpoint flights retain independent ACK state")) {
        return 1;
    }
    arwill_ipv4_tcp_release(&stack, first);
    arwill_ipv4_tcp_release(&stack, second);
    arwill_ipv4_tcp_release(&stack, third);

    const uint8_t active_peer_address[4] = { 10U, 0U, 2U, 2U };
    struct arwill_tcp_stream *active = arwill_ipv4_tcp_open(&stack);
    if (!expect(active != 0, "active endpoint allocated")
        || !expect(arwill_ipv4_tcp_bind(&stack, active, 26000U),
            "active endpoint binds local port")
        || !expect(arwill_ipv4_tcp_connect(&stack, active,
            active_peer_address, 26000U, 27000U) == 0,
            "active connect begins asynchronously")) {
        return 1;
    }
    if (!expect(arwill_ipv4_tcp_endpoint_snapshot(
            &stack, 1U, &endpoint_snapshot), "active endpoint snapshot")
        || !expect(text_equals(endpoint_snapshot.owner, "application") &&
            text_equals(endpoint_snapshot.state, "resolving-peer"),
            "active snapshot reports ARP resolution")
        || !expect(endpoint_snapshot.local_port == 26000U &&
            endpoint_snapshot.peer_port == 27000U,
            "active snapshot reports requested tuple")) {
        return 1;
    }
    const unsigned sends_before_arp = fake.send_count;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == sends_before_arp + 1U,
            "active connect sends ARP request")
        || !expect(get16(fake.outgoing, 12U) == 0x0806U &&
            get16(fake.outgoing, 20U) == 1U,
            "active connect emits an ARP request frame")) {
        return 1;
    }
    queue_arp_reply(&fake, active_peer_address);
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "active connect accepts ARP reply")
        || !expect(active->listener.state == arwill_tcp_state_syn_sent,
            "active connect enters SYN-SENT")
        || !expect(get16(fake.outgoing, 12U) == 0x0800U &&
            get16(fake.outgoing, 34U) == 26000U &&
            get16(fake.outgoing, 36U) == 27000U &&
            (fake.outgoing[47] & arwill_tcp_flag_syn) != 0U,
            "active connect emits SYN with requested tuple")) {
        return 1;
    }
    queue_segment_for_port(&fake, 27000U, 26000U, 9000U,
        active->listener.sequence + 1U,
        arwill_tcp_flag_syn | arwill_tcp_flag_ack, 0, 0U);
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "active connect accepts SYN-ACK")
        || !expect(arwill_ipv4_tcp_connect_status(&stack, active) == 1,
            "active connect reports established")
        || !expect(active->listener.state == arwill_tcp_state_established,
            "active endpoint enters established state")
        || !expect((fake.outgoing[47] & arwill_tcp_flag_ack) != 0U &&
            (fake.outgoing[47] & arwill_tcp_flag_syn) == 0U,
            "active connect completes with ACK")) {
        return 1;
    }
    arwill_ipv4_tcp_release(&stack, active);

    struct arwill_tcp_stream *unresolved = arwill_ipv4_tcp_open(&stack);
    const uint8_t missing_peer_address[4] = { 10U, 0U, 2U, 99U };
    if (!expect(unresolved != 0 &&
            arwill_ipv4_tcp_bind(&stack, unresolved, 26001U) &&
            arwill_ipv4_tcp_connect(&stack, unresolved,
                missing_peer_address, 26001U, 27001U) == 0,
            "unresolved active connect begins")) {
        return 1;
    }
    for (unsigned attempt = 0; attempt <= arwill_tcp_arp_max_attempts;
        attempt++) {
        (void)arwill_ipv4_poll_tcp(&stack);
        time.milliseconds += arwill_tcp_arp_retry_ms;
    }
    if (!expect(arwill_ipv4_tcp_connect_status(&stack, unresolved) == -1,
            "active connect reports bounded ARP failure")
        || !expect(arwill_ipv4_tcp_endpoint_snapshot(
            &stack, 1U, &endpoint_snapshot), "failed endpoint snapshot")
        || !expect(text_equals(endpoint_snapshot.state, "connect-failed"),
            "failed snapshot names terminal connect state")) {
        return 1;
    }
    arwill_ipv4_tcp_release(&stack, unresolved);

    struct arwill_udp_endpoint *udp = arwill_ipv4_udp_open(&stack);
    struct arwill_udp_endpoint *udp_second = arwill_ipv4_udp_open(&stack);
    struct arwill_udp_endpoint *udp_third = arwill_ipv4_udp_open(&stack);
    struct arwill_udp_endpoint *udp_fourth = arwill_ipv4_udp_open(&stack);
    const uint8_t dns_address[4] = { 10U, 0U, 2U, 3U };
    if (!expect(udp != 0 && udp_second != 0 && udp_third != 0 &&
            udp_fourth != 0, "four UDP endpoints allocated")
        || !expect(arwill_ipv4_udp_open(&stack) == 0,
            "UDP endpoint table is bounded")
        || !expect(arwill_ipv4_udp_bind(&stack, udp, 53000U),
            "UDP endpoint binds")
        || !expect(!arwill_ipv4_udp_bind(&stack, udp_second, 53000U),
            "duplicate UDP port rejected")
        || !expect(arwill_ipv4_udp_connect(
            &stack, udp, dns_address, 53U) == 0,
            "UDP connect begins asynchronously")) {
        return 1;
    }
    const unsigned sends_before_udp_arp = fake.send_count;
    (void)arwill_ipv4_poll_tcp(&stack);
    if (!expect(fake.send_count == sends_before_udp_arp + 1U,
            "UDP connect sends ARP request")
        || !expect(get16(fake.outgoing, 12U) == 0x0806U,
            "UDP connect emits ARP frame")) {
        return 1;
    }
    queue_arp_reply(&fake, dns_address);
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "UDP connect accepts ARP reply")
        || !expect(arwill_ipv4_udp_connect_status(&stack, udp) == 1,
            "UDP endpoint reports connected")) {
        return 1;
    }
    const uint8_t dns_query[] = { 0x12U, 0x34U, 0x01U, 0x00U };
    if (!expect(arwill_ipv4_udp_send(
            &stack, udp, dns_query, sizeof(dns_query)) == 1,
            "UDP datagram sends")
        || !expect(fake.outgoing[23] == 17U &&
            get16(fake.outgoing, 34U) == 53000U &&
            get16(fake.outgoing, 36U) == 53U,
            "UDP frame carries requested tuple")
        || !expect(udp_checksum(fake.outgoing + 14U,
            fake.outgoing + 34U, 8U + sizeof(dns_query)) == 0U,
            "UDP transmit checksum is valid")) {
        return 1;
    }
    const uint8_t dns_response[] = { 0x12U, 0x34U, 0x81U, 0x80U, 0x00U };
    queue_udp_datagram(&fake, dns_address, 53U, 53000U,
        dns_response, sizeof(dns_response));
    uint8_t udp_read[16];
    if (!expect(arwill_ipv4_poll_tcp(&stack),
            "UDP response dispatches")
        || !expect(arwill_ipv4_udp_receive(
            &stack, udp, udp_read, sizeof(udp_read)) ==
            (long)sizeof(dns_response), "UDP response reads atomically")
        || !expect(udp_read[0] == 0x12U && udp_read[3] == 0x80U,
            "UDP response preserves payload")
        || !expect(arwill_ipv4_udp_receive(
            &stack, udp, udp_read, sizeof(udp_read)) == 0L,
            "UDP receive is nonblocking")) {
        return 1;
    }
    queue_udp_datagram(&fake, dns_address, 54U, 53000U,
        dns_response, sizeof(dns_response));
    if (!expect(!arwill_ipv4_poll_tcp(&stack),
            "UDP response from wrong tuple is dropped")
        || !expect(stack.udp_port_drops == 1U,
            "UDP tuple drop is counted")) {
        return 1;
    }
    queue_udp_datagram(&fake, dns_address, 53U, 53000U,
        dns_response, sizeof(dns_response));
    fake.incoming[40] ^= 0x01U;
    if (!expect(!arwill_ipv4_poll_tcp(&stack),
            "UDP response with bad checksum is dropped")
        || !expect(stack.udp_checksum_drops == 1U,
            "UDP checksum drop is counted")) {
        return 1;
    }
    arwill_ipv4_udp_release(&stack, udp);
    arwill_ipv4_udp_release(&stack, udp_second);
    arwill_ipv4_udp_release(&stack, udp_third);
    arwill_ipv4_udp_release(&stack, udp_fourth);

    struct arwill_awp_network_owner first_owner;
    struct arwill_awp_network_owner second_owner;
    arwill_awp_network_owner_init(&first_owner);
    arwill_awp_network_owner_init(&second_owner);
    if (!expect(arwill_awp_network_open(&stack, &first_owner) == 0L &&
            arwill_awp_network_open(&stack, &first_owner) == 1L,
            "AWP owner receives two bounded handles")
        || !expect(arwill_awp_network_open(&stack, &first_owner) ==
            arwill_awp_network_unavailable,
            "AWP handle table is bounded")
        || !expect(arwill_awp_network_open(&stack, &second_owner) == 0L,
            "second AWP owner receives remaining endpoint")
        || !expect(arwill_awp_network_open(&stack, &second_owner) ==
            arwill_awp_network_unavailable,
            "application endpoint table is globally bounded")
        || !expect(arwill_awp_network_bind(&stack, &first_owner,
            0U, 28000U) == 0L, "AWP handle binds")
        || !expect(arwill_awp_network_bind(&stack, &second_owner,
            0U, 28000U) == arwill_awp_network_address_in_use,
            "AWP duplicate bind reports address in use")
        || !expect(arwill_awp_network_bind(&stack, &first_owner,
            1U, 28001U) == 0L &&
            arwill_awp_network_listen(&stack, &first_owner, 1U) == 0L,
            "AWP bound handle listens")
        || !expect(arwill_awp_network_accept(&first_owner, 1U) ==
            arwill_awp_network_retry,
            "AWP accept is nonblocking")
        || !expect(arwill_awp_network_write(&first_owner, 1U,
            output, arwill_awp_network_io_capacity + 1U) ==
            arwill_awp_network_invalid,
            "AWP write enforces syscall bound")) {
        return 1;
    }
    arwill_awp_network_cleanup(&stack, &first_owner);
    arwill_awp_network_cleanup(&stack, &second_owner);
    if (!expect(stack.endpoints[1].allocated == 0 &&
            stack.endpoints[2].allocated == 0 &&
            stack.endpoints[3].allocated == 0,
            "AWP cleanup releases every owned endpoint")) {
        return 1;
    }

    struct arwill_awp_network_owner udp_owner;
    arwill_awp_network_owner_init(&udp_owner);
    if (!expect(arwill_awp_udp_open(&stack, &udp_owner) == 0L,
            "AWP owner opens one UDP endpoint")
        || !expect(arwill_awp_udp_open(&stack, &udp_owner) ==
            arwill_awp_network_unavailable,
            "AWP owner UDP handle is bounded")
        || !expect(arwill_awp_udp_bind(
            &stack, &udp_owner, 53001U) == 0L,
            "AWP UDP endpoint binds")
        || !expect(arwill_awp_udp_connect(
            &stack, &udp_owner, 0x0a000203U, 53U) ==
            arwill_awp_network_retry,
            "AWP UDP connect yields for ARP")) {
        return 1;
    }
    arwill_awp_network_cleanup(&stack, &udp_owner);
    if (!expect(stack.udp_endpoints[0].allocated == 0,
            "AWP cleanup releases UDP endpoint")) {
        return 1;
    }

    puts("IPv4/ICMP/TCP/UDP host test passed");
    return 0;
}
