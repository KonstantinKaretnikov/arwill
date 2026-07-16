#include <stdint.h>

#include <arwill/kernel/input.h>

int arwill_input_try_read_byte(const struct arwill_input *input, uint8_t *byte) {
    if (input == 0 || input->try_read_byte == 0 || byte == 0) {
        return 0;
    }
    return input->try_read_byte(input->context, byte);
}
