#include <stddef.h>
#include <stdint.h>

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

static void write_char(const struct arwill_console *console, char value) {
    char text[2] = { value, '\0' };
    arwill_console_write(console, text);
}

int arwill_ipv4_init(struct arwill_ipv4_stack *stack,
    const struct arwill_network_device *network) {
    static const uint8_t address[4] = { 10, 0, 2, 15 };
    static const uint8_t gateway[4] = { 10, 0, 2, 2 };

    if (stack == 0 || network == 0 ||
        !arwill_network_read_mac(network, stack->mac)) {
        return 0;
    }
    stack->network = network;
    for (size_t index = 0; index < 4U; index++) {
        stack->address[index] = address[index];
        stack->gateway[index] = gateway[index];
    }
    stack->gateway_resolved = 0;
    stack->echo_identifier = 0x4152U;
    stack->echo_sequence = 0;
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
