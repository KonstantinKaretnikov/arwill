#ifndef ARWILL_KERNEL_IPV4_H
#define ARWILL_KERNEL_IPV4_H

#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/network.h>
#include <arwill/kernel/ssh.h>
#include <arwill/kernel/tcp.h>

struct arwill_ipv4_stack {
    const struct arwill_network_device *network;
    uint8_t mac[arwill_network_mac_length];
    uint8_t address[4];
    uint8_t gateway[4];
    uint8_t gateway_mac[arwill_network_mac_length];
    int gateway_resolved;
    uint16_t echo_identifier;
    uint16_t echo_sequence;
    struct arwill_tcp_listener tcp_listener;
    uint32_t tcp_frames_received;
    uint32_t tcp_syn_ack_sent;
    uint32_t ssh_banners_sent;
    uint32_t ssh_kexinit_build_failures;
    uint32_t ssh_kexinit_send_failures;
    uint32_t ssh_receive_failures;
    const struct arwill_ssh_host_key *ssh_host_key;
    struct arwill_ssh_transport ssh;
};

int arwill_ipv4_init(struct arwill_ipv4_stack *stack,
    const struct arwill_network_device *network,
    const struct arwill_ssh_host_key *ssh_host_key);

int arwill_ipv4_send_arp_request(const struct arwill_ipv4_stack *stack,
    const uint8_t target[4]);

int arwill_ipv4_ping_gateway(struct arwill_ipv4_stack *stack);

int arwill_ipv4_service_tcp(struct arwill_ipv4_stack *stack, size_t *frames_processed);
int arwill_ipv4_poll_tcp(struct arwill_ipv4_stack *stack);

void arwill_ipv4_print_config(const struct arwill_ipv4_stack *stack,
    const struct arwill_console *console);

#endif
