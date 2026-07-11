#include <stdint.h>

#include <arwill/kernel/input.h>

uint8_t arwill_input_read_byte(const struct arwill_input *input) {
    if (input == 0 || input->read_byte == 0) {
        return 0;
    }

    return input->read_byte(input->context);
}
