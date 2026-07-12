#ifndef ARWILL_KERNEL_SSH_H
#define ARWILL_KERNEL_SSH_H

#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/crypto.h>

struct arwill_filesystem;

enum {
    arwill_ssh_identification_capacity = 64,
    arwill_ssh_packet_capacity = 4096,
    arwill_ssh_server_packet_capacity = 1024,
    arwill_ssh_receive_capacity = 4096,
    arwill_ssh_host_key_blob_capacity = 128,
    arwill_ssh_signature_values_capacity = 80,
    arwill_ssh_signature_blob_capacity = 128,
    arwill_ssh_ecdh_reply_payload_capacity = 384,
};

enum arwill_ssh_host_key_error {
    arwill_ssh_host_key_error_none,
    arwill_ssh_host_key_error_storage,
    arwill_ssh_host_key_error_invalid,
    arwill_ssh_host_key_error_entropy,
    arwill_ssh_host_key_error_persist,
};

struct arwill_ssh_host_key {
    uint8_t private_key[arwill_p256_scalar_size];
    uint8_t public_key[arwill_p256_point_size];
    uint8_t fingerprint[arwill_sha256_size];
    int ready;
    int created;
    enum arwill_ssh_host_key_error error;
};

struct arwill_ssh_transport {
    char client_identification[arwill_ssh_identification_capacity];
    size_t client_identification_length;
    int client_identification_received;
    uint8_t receive_buffer[arwill_ssh_receive_capacity];
    size_t receive_length;
    uint8_t client_kexinit[arwill_ssh_packet_capacity];
    size_t client_kexinit_length;
    uint8_t server_kexinit[arwill_ssh_server_packet_capacity];
    size_t server_kexinit_length;
    uint8_t client_ephemeral[arwill_x25519_size];
    uint8_t server_ephemeral[arwill_x25519_size];
    uint8_t shared_secret[arwill_x25519_size];
    uint8_t exchange_hash[arwill_sha256_size];
    uint8_t kex_host_key_blob[arwill_ssh_host_key_blob_capacity];
    uint8_t kex_server_private[arwill_x25519_size];
    uint8_t kex_raw_signature[arwill_p256_signature_size];
    uint8_t kex_signature_values[arwill_ssh_signature_values_capacity];
    uint8_t kex_signature_blob[arwill_ssh_signature_blob_capacity];
    uint8_t kex_reply_payload[arwill_ssh_ecdh_reply_payload_capacity];
    struct arwill_sha256_context kex_hash;
    int client_kexinit_received;
    int server_kexinit_sent;
    int client_ecdh_init_received;
    int server_ecdh_reply_sent;
    uint32_t packets_received;
    uint32_t last_error;
};

int arwill_ssh_host_key_init(
    struct arwill_ssh_host_key *host_key,
    const struct arwill_filesystem *filesystem
);

void arwill_ssh_transport_init(struct arwill_ssh_transport *transport);
int arwill_ssh_transport_receive(
    struct arwill_ssh_transport *transport,
    const uint8_t *data,
    size_t length
);
int arwill_ssh_transport_build_kexinit(
    struct arwill_ssh_transport *transport,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_length
);
int arwill_ssh_transport_build_ecdh_reply(
    struct arwill_ssh_transport *transport,
    const struct arwill_ssh_host_key *host_key,
    uint8_t *packet,
    size_t capacity,
    size_t *packet_length
);

#endif
