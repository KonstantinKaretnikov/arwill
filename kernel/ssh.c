#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/entropy.h>
#include <arwill/kernel/ssh.h>

enum {
    ssh_message_kexinit = 20,
    ssh_minimum_padding = 4,
    ssh_clear_block_size = 8,
};

static void put32(uint8_t *buffer, size_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)(value >> 24U);
    buffer[offset + 1U] = (uint8_t)(value >> 16U);
    buffer[offset + 2U] = (uint8_t)(value >> 8U);
    buffer[offset + 3U] = (uint8_t)value;
}

static uint32_t get32(const uint8_t *buffer) {
    return ((uint32_t)buffer[0] << 24U)
        | ((uint32_t)buffer[1] << 16U)
        | ((uint32_t)buffer[2] << 8U)
        | (uint32_t)buffer[3];
}

static size_t text_length(const char *text) {
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static int append_bytes(
    uint8_t *buffer,
    size_t capacity,
    size_t *length,
    const uint8_t *data,
    size_t data_length
) {
    if (*length > capacity || data_length > capacity - *length) {
        return 0;
    }
    for (size_t index = 0; index < data_length; index++) {
        buffer[*length + index] = data[index];
    }
    *length += data_length;
    return 1;
}

static int append_u32(uint8_t *buffer, size_t capacity, size_t *length, uint32_t value) {
    uint8_t encoded[4];

    put32(encoded, 0U, value);
    return append_bytes(buffer, capacity, length, encoded, sizeof(encoded));
}

static int append_name_list(
    uint8_t *buffer,
    size_t capacity,
    size_t *length,
    const char *names
) {
    const size_t names_length = text_length(names);

    return names_length <= UINT32_MAX
        && append_u32(buffer, capacity, length, (uint32_t)names_length)
        && append_bytes(buffer, capacity, length, (const uint8_t *)names, names_length);
}

static void discard_received(struct arwill_ssh_transport *transport, size_t length) {
    if (length >= transport->receive_length) {
        transport->receive_length = 0;
        return;
    }
    for (size_t index = length; index < transport->receive_length; index++) {
        transport->receive_buffer[index - length] = transport->receive_buffer[index];
    }
    transport->receive_length -= length;
}

static int parse_identification(struct arwill_ssh_transport *transport) {
    static const char prefix[] = "SSH-2.0-";
    size_t line_length = 0;

    while (line_length < transport->receive_length
        && transport->receive_buffer[line_length] != (uint8_t)'\n') {
        line_length++;
    }
    if (line_length == transport->receive_length) {
        return 1;
    }
    if (line_length != 0U
        && transport->receive_buffer[line_length - 1U] == (uint8_t)'\r') {
        line_length--;
    }
    if (line_length < sizeof(prefix) - 1U
        || line_length >= sizeof(transport->client_identification)) {
        return 0;
    }
    for (size_t index = 0; index < sizeof(prefix) - 1U; index++) {
        if (transport->receive_buffer[index] != (uint8_t)prefix[index]) {
            return 0;
        }
    }
    for (size_t index = 0; index < line_length; index++) {
        transport->client_identification[index] =
            (char)transport->receive_buffer[index];
    }
    transport->client_identification[line_length] = '\0';
    transport->client_identification_length = line_length;
    transport->client_identification_received = 1;

    size_t consumed = line_length;
    if (transport->receive_buffer[consumed] == (uint8_t)'\r') {
        consumed++;
    }
    consumed++;
    discard_received(transport, consumed);
    return 1;
}

static int parse_packets(struct arwill_ssh_transport *transport) {
    while (transport->receive_length >= 5U) {
        const uint32_t encoded_packet_length = get32(transport->receive_buffer);
        const size_t packet_length = encoded_packet_length;

        if (packet_length < 1U + ssh_minimum_padding
            || packet_length > arwill_ssh_packet_capacity - 4U) {
            transport->last_error = 3U;
            return 0;
        }
        if (transport->receive_length < 4U + packet_length) {
            return 1;
        }

        const size_t padding_length = transport->receive_buffer[4];
        if (padding_length < ssh_minimum_padding || padding_length >= packet_length) {
            transport->last_error = 4U;
            return 0;
        }
        const size_t payload_length = packet_length - padding_length - 1U;
        if (payload_length == 0U) {
            transport->last_error = 5U;
            return 0;
        }

        transport->packets_received++;
        if (transport->receive_buffer[5] == ssh_message_kexinit
            && !transport->client_kexinit_received) {
            if (payload_length > sizeof(transport->client_kexinit)) {
                transport->last_error = 6U;
                return 0;
            }
            for (size_t index = 0; index < payload_length; index++) {
                transport->client_kexinit[index] = transport->receive_buffer[5U + index];
            }
            transport->client_kexinit_length = payload_length;
            transport->client_kexinit_received = 1;
        }

        discard_received(transport, 4U + packet_length);
    }
    return 1;
}

void arwill_ssh_transport_init(struct arwill_ssh_transport *transport) {
    if (transport == 0) {
        return;
    }
    transport->client_identification[0] = '\0';
    transport->client_identification_length = 0;
    transport->client_identification_received = 0;
    transport->receive_length = 0;
    transport->client_kexinit_length = 0;
    transport->server_kexinit_length = 0;
    transport->client_kexinit_received = 0;
    transport->server_kexinit_sent = 0;
    transport->packets_received = 0;
    transport->last_error = 0;
}

int arwill_ssh_transport_receive(
    struct arwill_ssh_transport *transport,
    const uint8_t *data,
    size_t length
) {
    if (transport == 0 || (data == 0 && length != 0U)
        || length > sizeof(transport->receive_buffer) - transport->receive_length) {
        if (transport != 0) {
            transport->last_error = 1U;
        }
        return 0;
    }
    if (!append_bytes(
        transport->receive_buffer,
        sizeof(transport->receive_buffer),
        &transport->receive_length,
        data,
        length
    )) {
        transport->last_error = 1U;
        return 0;
    }
    if (!transport->client_identification_received && !parse_identification(transport)) {
        transport->last_error = 2U;
        return 0;
    }
    if (!transport->client_identification_received) {
        return 1;
    }
    return parse_packets(transport);
}

int arwill_ssh_transport_build_kexinit(
    struct arwill_ssh_transport *transport,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_length
) {
    static const char kex[] = "curve25519-sha256";
    static const char host_key[] = "ecdsa-sha2-nistp256";
    static const char cipher[] = "chacha20-poly1305@openssh.com";
    static const char mac[] = "hmac-sha2-256";
    static const char compression[] = "none";
    uint8_t payload[512];
    size_t payload_length = 0;

    if (transport == 0 || packet == 0 || packet_length == 0) {
        return 0;
    }
    payload[payload_length++] = ssh_message_kexinit;
    if (!arwill_entropy_fill(payload + payload_length, 16U)) {
        return 0;
    }
    payload_length += 16U;
    if (!append_name_list(payload, sizeof(payload), &payload_length, kex)
        || !append_name_list(payload, sizeof(payload), &payload_length, host_key)
        || !append_name_list(payload, sizeof(payload), &payload_length, cipher)
        || !append_name_list(payload, sizeof(payload), &payload_length, cipher)
        || !append_name_list(payload, sizeof(payload), &payload_length, mac)
        || !append_name_list(payload, sizeof(payload), &payload_length, mac)
        || !append_name_list(payload, sizeof(payload), &payload_length, compression)
        || !append_name_list(payload, sizeof(payload), &payload_length, compression)
        || !append_name_list(payload, sizeof(payload), &payload_length, "")
        || !append_name_list(payload, sizeof(payload), &payload_length, "")
        || !append_bytes(
            payload,
            sizeof(payload),
            &payload_length,
            (const uint8_t[]){ 0U },
            1U
        )
        || !append_u32(payload, sizeof(payload), &payload_length, 0U)) {
        return 0;
    }

    size_t padding_length = ssh_clear_block_size
        - ((payload_length + 5U) % ssh_clear_block_size);
    if (padding_length < ssh_minimum_padding) {
        padding_length += ssh_clear_block_size;
    }
    const size_t encoded_packet_length = 1U + payload_length + padding_length;
    const size_t total_length = 4U + encoded_packet_length;
    if (encoded_packet_length > UINT32_MAX || total_length > capacity
        || payload_length > sizeof(transport->server_kexinit)) {
        return 0;
    }
    put32(packet, 0U, (uint32_t)encoded_packet_length);
    packet[4] = (uint8_t)padding_length;
    for (size_t index = 0; index < payload_length; index++) {
        packet[5U + index] = payload[index];
        transport->server_kexinit[index] = payload[index];
    }
    if (!arwill_entropy_fill(packet + 5U + payload_length, padding_length)) {
        return 0;
    }
    transport->server_kexinit_length = payload_length;
    *packet_length = total_length;
    return 1;
}
