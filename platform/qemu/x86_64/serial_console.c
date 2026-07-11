#include <stdint.h>

#include <arwill/arch/x86_64/io.h>
#include <arwill/kernel/console.h>
#include <arwill/platform/qemu/serial_console.h>

enum {
    com1_base = 0x3f8,
    register_data = 0,
    register_interrupt_enable = 1,
    register_fifo_control = 2,
    register_line_control = 3,
    register_modem_control = 4,
    register_line_status = 5,
    line_status_transmitter_empty = 0x20
};

static void serial_write_byte(uint8_t byte) {
    while ((arwill_x86_64_in8(com1_base + register_line_status) &
            line_status_transmitter_empty) == 0) {
    }

    arwill_x86_64_out8(com1_base + register_data, byte);
}

static void serial_console_write(void *context, const char *text) {
    (void)context;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == '\n') {
            serial_write_byte('\r');
        }

        serial_write_byte((uint8_t)*cursor);
    }
}

static const struct arwill_console serial_console = {
    .context = 0,
    .write = serial_console_write,
};

const struct arwill_console *arwill_qemu_serial_console_init(void) {
    arwill_x86_64_out8(com1_base + register_interrupt_enable, 0x00);
    arwill_x86_64_out8(com1_base + register_line_control, 0x80);
    arwill_x86_64_out8(com1_base + register_data, 0x01);
    arwill_x86_64_out8(com1_base + register_interrupt_enable, 0x00);
    arwill_x86_64_out8(com1_base + register_line_control, 0x03);
    arwill_x86_64_out8(com1_base + register_fifo_control, 0xc7);
    arwill_x86_64_out8(com1_base + register_modem_control, 0x03);

    return &serial_console;
}
