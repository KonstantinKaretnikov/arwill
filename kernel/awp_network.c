#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/awp_network.h>
#include <arwill/kernel/ipv4.h>
#include <arwill/kernel/tcp.h>
#include <arwill/kernel/tcp_stream.h>

static struct arwill_awp_network_handle *network_handle(
    struct arwill_awp_network_owner *owner, uint64_t handle) {
    if (owner == 0 || handle >= arwill_awp_network_handle_capacity) {
        return 0;
    }
    return &owner->handles[handle];
}

static void clear_handle(struct arwill_awp_network_handle *handle) {
    handle->stream = 0;
    handle->local_port = 0U;
    handle->state = arwill_awp_network_handle_free;
}

void arwill_awp_network_owner_init(struct arwill_awp_network_owner *owner) {
    if (owner == 0) {
        return;
    }
    for (size_t index = 0; index < arwill_awp_network_handle_capacity; index++) {
        clear_handle(&owner->handles[index]);
    }
}

void arwill_awp_network_cleanup(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner) {
    if (owner == 0) {
        return;
    }
    for (size_t index = 0; index < arwill_awp_network_handle_capacity; index++) {
        struct arwill_awp_network_handle *handle = &owner->handles[index];
        if (handle->stream != 0 && stack != 0) {
            arwill_ipv4_tcp_release(stack, handle->stream);
        }
        clear_handle(handle);
    }
}

long arwill_awp_network_open(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner) {
    if (stack == 0 || owner == 0) {
        return arwill_awp_network_unavailable;
    }
    for (size_t index = 0; index < arwill_awp_network_handle_capacity; index++) {
        struct arwill_awp_network_handle *handle = &owner->handles[index];
        if (handle->state == arwill_awp_network_handle_free) {
            handle->stream = arwill_ipv4_tcp_open(stack);
            if (handle->stream == 0) {
                return arwill_awp_network_unavailable;
            }
            handle->state = arwill_awp_network_handle_open;
            return (long)index;
        }
    }
    return arwill_awp_network_unavailable;
}

long arwill_awp_network_bind(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle_value,
    uint64_t port) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (stack == 0 || handle == 0 || handle->stream == 0 || port == 0U ||
        port > UINT16_MAX || handle->state != arwill_awp_network_handle_open) {
        return arwill_awp_network_invalid;
    }
    if (!arwill_ipv4_tcp_bind(stack, handle->stream, (uint16_t)port)) {
        return arwill_awp_network_address_in_use;
    }
    handle->local_port = (uint16_t)port;
    handle->state = arwill_awp_network_handle_bound;
    return 0L;
}

long arwill_awp_network_listen(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle_value) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (stack == 0 || handle == 0 || handle->stream == 0 ||
        handle->state != arwill_awp_network_handle_bound) {
        return arwill_awp_network_invalid;
    }
    if (!arwill_ipv4_tcp_listen(stack, handle->stream, handle->local_port)) {
        return arwill_awp_network_unavailable;
    }
    handle->state = arwill_awp_network_handle_listening;
    return 0L;
}

long arwill_awp_network_connect(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle_value,
    uint32_t peer_address, uint64_t peer_port) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (stack == 0 || handle == 0 || handle->stream == 0 ||
        peer_address == 0U || peer_port == 0U || peer_port > UINT16_MAX ||
        (handle->state != arwill_awp_network_handle_bound &&
            handle->state != arwill_awp_network_handle_connecting)) {
        return arwill_awp_network_invalid;
    }
    if (handle->state == arwill_awp_network_handle_connecting) {
        const int status = arwill_ipv4_tcp_connect_status(stack, handle->stream);
        if (status > 0) {
            return 0L;
        }
        return status < 0 ? arwill_awp_network_unavailable :
            arwill_awp_network_retry;
    }
    const uint8_t address[4] = {
        (uint8_t)(peer_address >> 24U),
        (uint8_t)(peer_address >> 16U),
        (uint8_t)(peer_address >> 8U),
        (uint8_t)peer_address
    };
    const int status = arwill_ipv4_tcp_connect(stack, handle->stream, address,
        handle->local_port, (uint16_t)peer_port);
    if (status < 0) {
        return arwill_awp_network_unavailable;
    }
    handle->state = arwill_awp_network_handle_connecting;
    return status > 0 ? 0L : arwill_awp_network_retry;
}

long arwill_awp_network_accept(struct arwill_awp_network_owner *owner,
    uint64_t handle_value) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (handle == 0 || handle->stream == 0 ||
        handle->state != arwill_awp_network_handle_listening) {
        return arwill_awp_network_invalid;
    }
    return arwill_tcp_stream_connected(handle->stream) ? (long)handle_value :
        arwill_awp_network_retry;
}

long arwill_awp_network_read(struct arwill_awp_network_owner *owner,
    uint64_t handle_value, uint8_t *buffer, size_t capacity) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (handle == 0 || handle->stream == 0 || buffer == 0 || capacity == 0U ||
        capacity > arwill_awp_network_io_capacity ||
        (handle->state != arwill_awp_network_handle_listening &&
            handle->state != arwill_awp_network_handle_connecting &&
            handle->state != arwill_awp_network_handle_closing)) {
        return arwill_awp_network_invalid;
    }
    const size_t read = arwill_tcp_stream_read(handle->stream, buffer, capacity);
    if (read != 0U) {
        return (long)read;
    }
    if (!arwill_tcp_stream_connected(handle->stream) &&
        handle->stream->connections != 0U) {
        return arwill_awp_network_closed;
    }
    return arwill_awp_network_retry;
}

long arwill_awp_network_write(struct arwill_awp_network_owner *owner,
    uint64_t handle_value, const uint8_t *buffer, size_t length) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (handle == 0 || handle->stream == 0 || buffer == 0 || length == 0U ||
        length > arwill_awp_network_io_capacity ||
        (handle->state != arwill_awp_network_handle_listening &&
            handle->state != arwill_awp_network_handle_connecting)) {
        return arwill_awp_network_invalid;
    }
    if (!arwill_tcp_stream_connected(handle->stream)) {
        return handle->stream->connections == 0U ? arwill_awp_network_retry :
            arwill_awp_network_closed;
    }
    const size_t written = arwill_tcp_stream_write(handle->stream, buffer, length);
    return written == 0U ? arwill_awp_network_retry : (long)written;
}

long arwill_awp_network_close(struct arwill_ipv4_stack *stack,
    struct arwill_awp_network_owner *owner, uint64_t handle_value) {
    struct arwill_awp_network_handle *handle =
        network_handle(owner, handle_value);
    if (stack == 0 || handle == 0 || handle->stream == 0 ||
        handle->state == arwill_awp_network_handle_free) {
        return arwill_awp_network_invalid;
    }
    if (handle->state == arwill_awp_network_handle_closing) {
        if (handle->stream->listener.state != arwill_tcp_state_listen ||
            handle->stream->close_requested) {
            return arwill_awp_network_retry;
        }
        arwill_ipv4_tcp_release(stack, handle->stream);
        clear_handle(handle);
        return 0L;
    }
    if (arwill_tcp_stream_connected(handle->stream)) {
        arwill_tcp_stream_close(handle->stream);
        handle->state = arwill_awp_network_handle_closing;
        return arwill_awp_network_retry;
    }
    arwill_ipv4_tcp_release(stack, handle->stream);
    clear_handle(handle);
    return 0L;
}
