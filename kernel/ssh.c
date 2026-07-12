#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/crypto.h>
#include <arwill/kernel/entropy.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/ssh.h>

enum {
    ssh_message_kexinit = 20,
    ssh_message_kex_ecdh_init = 30,
    ssh_message_kex_ecdh_reply = 31,
    ssh_minimum_padding = 4,
    ssh_clear_block_size = 8,
    ssh_host_key_generation_attempts = 16,
};

static const char ssh_host_key_path[] = "/system/ssh-host-key";
static const char ssh_server_identification[] = "SSH-2.0-Arwill";
static const char ssh_host_key_algorithm[] = "ecdsa-sha2-nistp256";
static const char ssh_host_key_curve[] = "nistp256";

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

static int append_string(
    uint8_t *buffer,
    size_t capacity,
    size_t *length,
    const uint8_t *data,
    size_t data_length
) {
    return data_length <= UINT32_MAX
        && append_u32(buffer, capacity, length, (uint32_t)data_length)
        && append_bytes(buffer, capacity, length, data, data_length);
}

static int append_mpint(
    uint8_t *buffer,
    size_t capacity,
    size_t *length,
    const uint8_t *value,
    size_t value_length
) {
    size_t first = 0;

    while (first < value_length && value[first] == 0U) {
        first++;
    }
    if (first == value_length) {
        return append_u32(buffer, capacity, length, 0U);
    }

    const int prefix_zero = (value[first] & 0x80U) != 0U;
    const size_t encoded_length = value_length - first + (prefix_zero ? 1U : 0U);
    if (encoded_length > UINT32_MAX
        || !append_u32(buffer, capacity, length, (uint32_t)encoded_length)) {
        return 0;
    }
    if (prefix_zero && !append_bytes(
        buffer,
        capacity,
        length,
        (const uint8_t[]){ 0U },
        1U
    )) {
        return 0;
    }
    return append_bytes(buffer, capacity, length, value + first, value_length - first);
}

static int build_host_key_blob(
    const struct arwill_ssh_host_key *host_key,
    uint8_t *blob,
    size_t capacity,
    size_t *length
) {
    *length = 0;
    return host_key != 0
        && append_name_list(blob, capacity, length, ssh_host_key_algorithm)
        && append_name_list(blob, capacity, length, ssh_host_key_curve)
        && append_string(
            blob,
            capacity,
            length,
            host_key->public_key,
            sizeof(host_key->public_key)
        );
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

static void clear_host_key(struct arwill_ssh_host_key *host_key) {
    for (size_t index = 0; index < sizeof(host_key->private_key); index++) {
        host_key->private_key[index] = 0;
    }
    for (size_t index = 0; index < sizeof(host_key->public_key); index++) {
        host_key->public_key[index] = 0;
    }
    for (size_t index = 0; index < sizeof(host_key->fingerprint); index++) {
        host_key->fingerprint[index] = 0;
    }
    host_key->ready = 0;
    host_key->created = 0;
    host_key->error = arwill_ssh_host_key_error_none;
}

static int private_scalar_is_valid(const uint8_t scalar[arwill_p256_scalar_size]) {
    static const uint8_t order[arwill_p256_scalar_size] = {
        0xffU, 0xffU, 0xffU, 0xffU, 0x00U, 0x00U, 0x00U, 0x00U,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xbcU, 0xe6U, 0xfaU, 0xadU, 0xa7U, 0x17U, 0x9eU, 0x84U,
        0xf3U, 0xb9U, 0xcaU, 0xc2U, 0xfcU, 0x63U, 0x25U, 0x51U,
    };
    int nonzero = 0;

    for (size_t index = 0; index < arwill_p256_scalar_size; index++) {
        nonzero |= scalar[index] != 0U;
    }
    if (!nonzero) {
        return 0;
    }
    for (size_t index = 0; index < arwill_p256_scalar_size; index++) {
        if (scalar[index] < order[index]) {
            return 1;
        }
        if (scalar[index] > order[index]) {
            return 0;
        }
    }
    return 0;
}

static int build_host_key_fingerprint(struct arwill_ssh_host_key *host_key) {
    uint8_t blob[128];
    size_t length = 0;

    if (!build_host_key_blob(host_key, blob, sizeof(blob), &length)) {
        return 0;
    }
    arwill_crypto_sha256(blob, length, host_key->fingerprint);
    return 1;
}

static int initialize_host_key_material(struct arwill_ssh_host_key *host_key) {
    return private_scalar_is_valid(host_key->private_key)
        && arwill_crypto_p256_public(host_key->public_key, host_key->private_key)
        && build_host_key_fingerprint(host_key);
}

static int host_key_file_exists(const struct arwill_filesystem *filesystem) {
    struct arwill_fs_listing listing;

    if (!arwill_filesystem_list(filesystem, "/system", &listing)) {
        return -1;
    }
    for (size_t index = 0; index < listing.count; index++) {
        if (text_equals(listing.entries[index].name, "ssh-host-key")) {
            return 1;
        }
    }
    return 0;
}

int arwill_ssh_host_key_init(
    struct arwill_ssh_host_key *host_key,
    const struct arwill_filesystem *filesystem
) {
    struct arwill_fs_file file;

    if (host_key == 0) {
        return 0;
    }
    clear_host_key(host_key);

    const int exists = host_key_file_exists(filesystem);
    if (exists < 0) {
        host_key->error = arwill_ssh_host_key_error_storage;
        return 0;
    }
    if (exists != 0) {
        if (!arwill_filesystem_read_file(filesystem, ssh_host_key_path, &file)
            || file.type != arwill_fs_file_binary
            || file.size_bytes != (uint64_t)arwill_p256_scalar_size
            || file.contents == 0) {
            host_key->error = arwill_ssh_host_key_error_invalid;
            return 0;
        }
        for (size_t index = 0; index < sizeof(host_key->private_key); index++) {
            host_key->private_key[index] = (uint8_t)file.contents[index];
        }
        if (!initialize_host_key_material(host_key)) {
            clear_host_key(host_key);
            host_key->error = arwill_ssh_host_key_error_invalid;
            return 0;
        }
        host_key->ready = 1;
        return 1;
    }

    for (size_t attempt = 0; attempt < ssh_host_key_generation_attempts; attempt++) {
        if (!arwill_entropy_fill(host_key->private_key, sizeof(host_key->private_key))) {
            clear_host_key(host_key);
            host_key->error = arwill_ssh_host_key_error_entropy;
            return 0;
        }
        if (initialize_host_key_material(host_key)) {
            if (!arwill_filesystem_write_bytes(
                filesystem,
                ssh_host_key_path,
                arwill_fs_file_binary,
                host_key->private_key,
                sizeof(host_key->private_key)
            )) {
                clear_host_key(host_key);
                host_key->error = arwill_ssh_host_key_error_persist;
                return 0;
            }
            host_key->ready = 1;
            host_key->created = 1;
            return 1;
        }
    }

    clear_host_key(host_key);
    host_key->error = arwill_ssh_host_key_error_entropy;
    return 0;
}

static void hash_u32(struct arwill_sha256_context *context, uint32_t value) {
    uint8_t encoded[4];

    put32(encoded, 0U, value);
    arwill_crypto_sha256_update(context, encoded, sizeof(encoded));
}

static int hash_string(
    struct arwill_sha256_context *context,
    const uint8_t *data,
    size_t length
) {
    if (length > UINT32_MAX) {
        return 0;
    }
    hash_u32(context, (uint32_t)length);
    arwill_crypto_sha256_update(context, data, length);
    return 1;
}

static int hash_mpint(
    struct arwill_sha256_context *context,
    const uint8_t *value,
    size_t value_length
) {
    size_t first = 0;

    while (first < value_length && value[first] == 0U) {
        first++;
    }
    if (first == value_length) {
        hash_u32(context, 0U);
        return 1;
    }

    const int prefix_zero = (value[first] & 0x80U) != 0U;
    const size_t encoded_length = value_length - first + (prefix_zero ? 1U : 0U);
    if (encoded_length > UINT32_MAX) {
        return 0;
    }
    hash_u32(context, (uint32_t)encoded_length);
    if (prefix_zero) {
        static const uint8_t zero = 0U;
        arwill_crypto_sha256_update(context, &zero, 1U);
    }
    arwill_crypto_sha256_update(context, value + first, value_length - first);
    return 1;
}

static int build_clear_packet(
    const uint8_t *payload,
    size_t payload_length,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_length
) {
    size_t padding_length = ssh_clear_block_size
        - ((payload_length + 5U) % ssh_clear_block_size);
    if (padding_length < ssh_minimum_padding) {
        padding_length += ssh_clear_block_size;
    }
    const size_t encoded_packet_length = 1U + payload_length + padding_length;
    const size_t total_length = 4U + encoded_packet_length;
    if (encoded_packet_length > UINT32_MAX || total_length > capacity) {
        return 0;
    }
    put32(packet, 0U, (uint32_t)encoded_packet_length);
    packet[4] = (uint8_t)padding_length;
    for (size_t index = 0; index < payload_length; index++) {
        packet[5U + index] = payload[index];
    }
    if (!arwill_entropy_fill(packet + 5U + payload_length, padding_length)) {
        return 0;
    }
    *packet_length = total_length;
    return 1;
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
        } else if (transport->receive_buffer[5] == ssh_message_kex_ecdh_init
            && !transport->client_ecdh_init_received) {
            if (!transport->client_kexinit_received || payload_length != 37U
                || get32(transport->receive_buffer + 6U) != (uint32_t)arwill_x25519_size) {
                transport->last_error = 7U;
                return 0;
            }
            for (size_t index = 0; index < sizeof(transport->client_ephemeral); index++) {
                transport->client_ephemeral[index] = transport->receive_buffer[10U + index];
            }
            transport->client_ecdh_init_received = 1;
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
    transport->client_ecdh_init_received = 0;
    transport->server_ecdh_reply_sent = 0;
    for (size_t index = 0; index < sizeof(transport->client_ephemeral); index++) {
        transport->client_ephemeral[index] = 0;
        transport->server_ephemeral[index] = 0;
        transport->shared_secret[index] = 0;
        transport->exchange_hash[index] = 0;
    }
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

    if (payload_length > sizeof(transport->server_kexinit)) {
        return 0;
    }
    for (size_t index = 0; index < payload_length; index++) {
        transport->server_kexinit[index] = payload[index];
    }
    if (!build_clear_packet(
        payload,
        payload_length,
        packet,
        capacity,
        packet_length
    )) {
        return 0;
    }
    transport->server_kexinit_length = payload_length;
    return 1;
}

int arwill_ssh_transport_build_ecdh_reply(
    struct arwill_ssh_transport *transport,
    const struct arwill_ssh_host_key *host_key,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_length
) {
    size_t host_key_blob_length = 0;
    size_t signature_values_length = 0;
    size_t signature_blob_length = 0;
    size_t payload_length = 0;

    if (transport == 0 || host_key == 0 || !host_key->ready || packet == 0
        || packet_length == 0 || !transport->client_identification_received
        || !transport->client_kexinit_received || !transport->server_kexinit_sent
        || !transport->client_ecdh_init_received || transport->server_ecdh_reply_sent
        || !build_host_key_blob(
            host_key,
            transport->kex_host_key_blob,
            sizeof(transport->kex_host_key_blob),
            &host_key_blob_length
        )
        || !arwill_entropy_fill(
            transport->kex_server_private,
            sizeof(transport->kex_server_private)
        )) {
        return 0;
    }

    const int key_agreement_ready = arwill_crypto_x25519_public(
        transport->server_ephemeral,
        transport->kex_server_private
    ) && arwill_crypto_x25519(
        transport->shared_secret,
        transport->kex_server_private,
        transport->client_ephemeral
    );
    for (size_t index = 0; index < sizeof(transport->kex_server_private); index++) {
        transport->kex_server_private[index] = 0;
    }
    if (!key_agreement_ready) {
        return 0;
    }

    arwill_crypto_sha256_init(&transport->kex_hash);
    if (!hash_string(
            &transport->kex_hash,
            (const uint8_t *)transport->client_identification,
            transport->client_identification_length
        )
        || !hash_string(
            &transport->kex_hash,
            (const uint8_t *)ssh_server_identification,
            sizeof(ssh_server_identification) - 1U
        )
        || !hash_string(
            &transport->kex_hash,
            transport->client_kexinit,
            transport->client_kexinit_length
        )
        || !hash_string(
            &transport->kex_hash,
            transport->server_kexinit,
            transport->server_kexinit_length
        )
        || !hash_string(
            &transport->kex_hash,
            transport->kex_host_key_blob,
            host_key_blob_length
        )
        || !hash_string(
            &transport->kex_hash,
            transport->client_ephemeral,
            sizeof(transport->client_ephemeral)
        )
        || !hash_string(
            &transport->kex_hash,
            transport->server_ephemeral,
            sizeof(transport->server_ephemeral)
        )
        || !hash_mpint(
            &transport->kex_hash,
            transport->shared_secret,
            sizeof(transport->shared_secret)
        )) {
        return 0;
    }
    arwill_crypto_sha256_finish(&transport->kex_hash, transport->exchange_hash);

    if (!arwill_crypto_p256_sign(
            transport->kex_raw_signature,
            host_key->private_key,
            transport->exchange_hash
        )
        || !append_mpint(
            transport->kex_signature_values,
            sizeof(transport->kex_signature_values),
            &signature_values_length,
            transport->kex_raw_signature,
            arwill_p256_scalar_size
        )
        || !append_mpint(
            transport->kex_signature_values,
            sizeof(transport->kex_signature_values),
            &signature_values_length,
            transport->kex_raw_signature + arwill_p256_scalar_size,
            arwill_p256_scalar_size
        )
        || !append_name_list(
            transport->kex_signature_blob,
            sizeof(transport->kex_signature_blob),
            &signature_blob_length,
            ssh_host_key_algorithm
        )
        || !append_string(
            transport->kex_signature_blob,
            sizeof(transport->kex_signature_blob),
            &signature_blob_length,
            transport->kex_signature_values,
            signature_values_length
        )) {
        return 0;
    }

    transport->kex_reply_payload[payload_length++] = ssh_message_kex_ecdh_reply;
    if (!append_string(
            transport->kex_reply_payload,
            sizeof(transport->kex_reply_payload),
            &payload_length,
            transport->kex_host_key_blob,
            host_key_blob_length
        )
        || !append_string(
            transport->kex_reply_payload,
            sizeof(transport->kex_reply_payload),
            &payload_length,
            transport->server_ephemeral,
            sizeof(transport->server_ephemeral)
        )
        || !append_string(
            transport->kex_reply_payload,
            sizeof(transport->kex_reply_payload),
            &payload_length,
            transport->kex_signature_blob,
            signature_blob_length
        )) {
        return 0;
    }
    return build_clear_packet(
        transport->kex_reply_payload,
        payload_length,
        packet,
        capacity,
        packet_length
    );
}
