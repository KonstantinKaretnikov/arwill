#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/network.h>

int arwill_network_send_frame(
    const struct arwill_network_device *network,
    const uint8_t *frame,
    size_t length
) {
    if (network == 0 || network->send_frame == 0 || frame == 0 ||
        length == 0U || length > arwill_network_frame_capacity) {
        return 0;
    }

    return network->send_frame(network->context, frame, length);
}

int arwill_network_poll_frame(
    const struct arwill_network_device *network,
    uint8_t *frame,
    size_t capacity,
    size_t *length
) {
    if (length == 0) {
        return 0;
    }

    *length = 0;
    if (network == 0 || network->poll_frame == 0 || frame == 0 || capacity == 0U) {
        return 0;
    }

    return network->poll_frame(network->context, frame, capacity, length);
}

int arwill_network_read_mac(
    const struct arwill_network_device *network,
    uint8_t mac[arwill_network_mac_length]
) {
    if (network == 0 || network->read_mac == 0 || mac == 0) {
        return 0;
    }

    return network->read_mac(network->context, mac);
}
