#ifndef ARWILL_KERNEL_NETWORK_H
#define ARWILL_KERNEL_NETWORK_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_network_mac_length = 6,
    arwill_network_frame_capacity = 1536,
};

struct arwill_network_device {
    const char *name;
    void *context;
    int (*send_frame)(void *context, const uint8_t *frame, size_t length);
    int (*poll_frame)(
        void *context,
        uint8_t *frame,
        size_t capacity,
        size_t *length
    );
    int (*read_mac)(void *context, uint8_t mac[arwill_network_mac_length]);
};

int arwill_network_send_frame(
    const struct arwill_network_device *network,
    const uint8_t *frame,
    size_t length
);

int arwill_network_poll_frame(
    const struct arwill_network_device *network,
    uint8_t *frame,
    size_t capacity,
    size_t *length
);

int arwill_network_read_mac(
    const struct arwill_network_device *network,
    uint8_t mac[arwill_network_mac_length]
);

#endif
