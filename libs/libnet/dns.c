#include <arwill/user/dns.h>

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] << 8U) | data[1];
}

static void write_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static int skip_name(
    const uint8_t *message,
    size_t length,
    size_t *offset
) {
    size_t position = *offset;
    size_t labels = 0;
    while (position < length) {
        const uint8_t part = message[position++];
        if (part == 0U) {
            *offset = position;
            return 1;
        }
        if ((part & 0xc0U) == 0xc0U) {
            if (position >= length) {
                return 0;
            }
            *offset = position + 1U;
            return 1;
        }
        if ((part & 0xc0U) != 0U || part > 63U ||
            position + part > length || ++labels > 127U) {
            return 0;
        }
        position += part;
    }
    return 0;
}

int arwill_dns_build_a_query(
    const char *host,
    uint16_t identifier,
    uint8_t *output,
    size_t capacity,
    size_t *length
) {
    if (host == 0 || output == 0 || length == 0 || host[0] == '\0' ||
        capacity < 17U) {
        return arwill_dns_invalid;
    }
    for (size_t index = 0; index < 12U; index++) {
        output[index] = 0U;
    }
    write_u16(&output[0], identifier);
    write_u16(&output[2], 0x0100U);
    write_u16(&output[4], 1U);

    size_t used = 12U;
    size_t start = 0U;
    size_t index = 0U;
    while (1) {
        if (host[index] != '.' && host[index] != '\0') {
            const char value = host[index];
            if (!((value >= 'a' && value <= 'z') ||
                    (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') || value == '-')) {
                return arwill_dns_invalid;
            }
            index++;
            continue;
        }
        const size_t label_length = index - start;
        if (label_length == 0U || label_length > 63U ||
            used + 1U + label_length + 5U > capacity) {
            return label_length > 63U ? arwill_dns_invalid :
                arwill_dns_too_large;
        }
        output[used++] = (uint8_t)label_length;
        for (size_t label = start; label < index; label++) {
            output[used++] = (uint8_t)host[label];
        }
        if (host[index] == '\0') {
            break;
        }
        start = ++index;
    }
    output[used++] = 0U;
    write_u16(&output[used], 1U);
    write_u16(&output[used + 2U], 1U);
    used += 4U;
    *length = used;
    return arwill_dns_ok;
}

int arwill_dns_parse_a_response(
    const uint8_t *message,
    size_t length,
    uint16_t identifier,
    uint32_t *address
) {
    if (message == 0 || address == 0 || length < 12U ||
        read_u16(&message[0]) != identifier ||
        (read_u16(&message[2]) & 0x8000U) == 0U) {
        return arwill_dns_invalid;
    }
    const uint16_t flags = read_u16(&message[2]);
    if ((flags & 0x000fU) != 0U) {
        return (flags & 0x000fU) == 3U ? arwill_dns_not_found :
            arwill_dns_server_failure;
    }
    const uint16_t question_count = read_u16(&message[4]);
    const uint16_t answer_count = read_u16(&message[6]);
    size_t offset = 12U;
    for (uint16_t question = 0U; question < question_count; question++) {
        if (!skip_name(message, length, &offset) || offset + 4U > length) {
            return arwill_dns_invalid;
        }
        offset += 4U;
    }
    for (uint16_t answer_index = 0U;
        answer_index < answer_count;
        answer_index++) {
        if (!skip_name(message, length, &offset) || offset + 10U > length) {
            return arwill_dns_invalid;
        }
        const uint16_t type = read_u16(&message[offset]);
        const uint16_t record_class = read_u16(&message[offset + 2U]);
        const uint16_t data_length = read_u16(&message[offset + 8U]);
        offset += 10U;
        if (offset + data_length > length) {
            return arwill_dns_invalid;
        }
        if (type == 1U && record_class == 1U && data_length == 4U) {
            *address = (uint32_t)message[offset] << 24U |
                (uint32_t)message[offset + 1U] << 16U |
                (uint32_t)message[offset + 2U] << 8U |
                message[offset + 3U];
            return arwill_dns_ok;
        }
        offset += data_length;
    }
    return arwill_dns_not_found;
}
