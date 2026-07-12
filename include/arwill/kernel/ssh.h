#ifndef ARWILL_KERNEL_SSH_H
#define ARWILL_KERNEL_SSH_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_ssh_identification_capacity = 64,
    arwill_ssh_packet_capacity = 4096,
    arwill_ssh_server_packet_capacity = 1024,
    arwill_ssh_receive_capacity = 4096,
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
    int client_kexinit_received;
    int server_kexinit_sent;
    uint32_t packets_received;
    uint32_t last_error;
};

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

#endif
