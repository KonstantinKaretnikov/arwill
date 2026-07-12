#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/io.h>
#include <arwill/kernel/block_device.h>
#include <arwill/platform/qemu/ata_pio.h>

enum {
    ata_sector_size = 512,
    ata_words_per_sector = 256,
    ata_primary_io_base = 0x1f0,
    ata_primary_control_base = 0x3f6,
    ata_register_data = 0,
    ata_register_error = 1,
    ata_register_sector_count = 2,
    ata_register_lba_low = 3,
    ata_register_lba_mid = 4,
    ata_register_lba_high = 5,
    ata_register_drive = 6,
    ata_register_status_command = 7,
    ata_status_error = 0x01,
    ata_status_data_request = 0x08,
    ata_status_busy = 0x80,
    ata_drive_master_lba = 0xe0,
    ata_command_identify = 0xec,
    ata_command_read_sectors = 0x20,
    ata_command_write_sectors = 0x30,
    ata_command_cache_flush = 0xe7,
    ata_timeout_iterations = 1000000
};

struct qemu_ata_context {
    int present;
    uint64_t sector_count;
};

static struct qemu_ata_context ata_context;

static void ata_io_wait(void) {
    (void)arwill_x86_64_in8(ata_primary_control_base);
    (void)arwill_x86_64_in8(ata_primary_control_base);
    (void)arwill_x86_64_in8(ata_primary_control_base);
    (void)arwill_x86_64_in8(ata_primary_control_base);
}

static uint8_t ata_status(void) {
    return arwill_x86_64_in8(ata_primary_io_base + ata_register_status_command);
}

static int ata_wait_not_busy(void) {
    for (uint32_t index = 0; index < ata_timeout_iterations; index++) {
        const uint8_t status = ata_status();

        if (status == 0xffU) {
            return 0;
        }

        if ((status & ata_status_busy) == 0U) {
            return 1;
        }
    }

    return 0;
}

static int ata_wait_data_request(void) {
    for (uint32_t index = 0; index < ata_timeout_iterations; index++) {
        const uint8_t status = ata_status();

        if (status == 0xffU || (status & ata_status_error) != 0U) {
            return 0;
        }

        if ((status & ata_status_busy) == 0U &&
            (status & ata_status_data_request) != 0U) {
            return 1;
        }
    }

    return 0;
}

static void ata_select_master(void) {
    arwill_x86_64_out8(ata_primary_io_base + ata_register_drive, 0xa0);
    ata_io_wait();
}

static void ata_read_words(uint16_t *words, size_t word_count) {
    for (size_t index = 0; index < word_count; index++) {
        words[index] = arwill_x86_64_in16(ata_primary_io_base + ata_register_data);
    }
}

static void ata_read_sector_bytes(uint8_t *buffer) {
    for (size_t index = 0; index < ata_words_per_sector; index++) {
        const uint16_t word = arwill_x86_64_in16(ata_primary_io_base + ata_register_data);

        buffer[index * 2U] = (uint8_t)(word & 0xffU);
        buffer[(index * 2U) + 1U] = (uint8_t)(word >> 8U);
    }
}

static void ata_write_sector_bytes(const uint8_t *buffer) {
    for (size_t index = 0; index < ata_words_per_sector; index++) {
        const uint16_t word =
            (uint16_t)(
                (uint16_t)buffer[index * 2U] |
                ((uint16_t)buffer[(index * 2U) + 1U] << 8U)
            );

        arwill_x86_64_out16(ata_primary_io_base + ata_register_data, word);
    }
}

static int ata_identify(uint64_t *sector_count) {
    uint16_t identify[256];

    ata_select_master();

    arwill_x86_64_out8(ata_primary_io_base + ata_register_sector_count, 0);
    arwill_x86_64_out8(ata_primary_io_base + ata_register_lba_low, 0);
    arwill_x86_64_out8(ata_primary_io_base + ata_register_lba_mid, 0);
    arwill_x86_64_out8(ata_primary_io_base + ata_register_lba_high, 0);
    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_status_command,
        ata_command_identify
    );

    if (ata_status() == 0U || ata_status() == 0xffU) {
        return 0;
    }

    if (!ata_wait_not_busy()) {
        return 0;
    }

    if (arwill_x86_64_in8(ata_primary_io_base + ata_register_lba_mid) != 0U ||
        arwill_x86_64_in8(ata_primary_io_base + ata_register_lba_high) != 0U) {
        return 0;
    }

    if (!ata_wait_data_request()) {
        return 0;
    }

    ata_read_words(identify, sizeof(identify) / sizeof(identify[0]));

    const uint64_t lba28_sector_count =
        (uint64_t)identify[60] | ((uint64_t)identify[61] << 16U);

    if (lba28_sector_count == 0U) {
        return 0;
    }

    *sector_count = lba28_sector_count;
    return 1;
}

static void ata_select_lba28(uint32_t lba) {
    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_drive,
        (uint8_t)(ata_drive_master_lba | ((lba >> 24U) & 0x0fU))
    );
    ata_io_wait();

    arwill_x86_64_out8(ata_primary_io_base + ata_register_sector_count, 1);
    arwill_x86_64_out8(ata_primary_io_base + ata_register_lba_low, (uint8_t)lba);
    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_lba_mid,
        (uint8_t)(lba >> 8U)
    );
    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_lba_high,
        (uint8_t)(lba >> 16U)
    );
}

static int ata_read_one_sector(uint32_t lba, uint8_t *buffer) {
    if (!ata_wait_not_busy()) {
        return 0;
    }

    ata_select_lba28(lba);
    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_status_command,
        ata_command_read_sectors
    );

    if (!ata_wait_data_request()) {
        return 0;
    }

    ata_read_sector_bytes(buffer);
    return 1;
}

static int ata_flush_cache(void) {
    if (!ata_wait_not_busy()) {
        return 0;
    }

    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_status_command,
        ata_command_cache_flush
    );

    return ata_wait_not_busy();
}

static int ata_write_one_sector(uint32_t lba, const uint8_t *buffer) {
    if (!ata_wait_not_busy()) {
        return 0;
    }

    ata_select_lba28(lba);
    arwill_x86_64_out8(
        ata_primary_io_base + ata_register_status_command,
        ata_command_write_sectors
    );

    if (!ata_wait_data_request()) {
        return 0;
    }

    ata_write_sector_bytes(buffer);
    return ata_flush_cache();
}

static int qemu_ata_read(
    void *context,
    uint64_t lba,
    uint32_t sector_count,
    uint8_t *buffer,
    size_t buffer_size
) {
    (void)buffer_size;

    const struct qemu_ata_context *ata = (const struct qemu_ata_context *)context;

    if (ata == 0 || !ata->present || lba > UINT32_MAX) {
        return 0;
    }

    for (uint32_t index = 0; index < sector_count; index++) {
        const uint64_t current_lba = lba + (uint64_t)index;

        if (current_lba > UINT32_MAX) {
            return 0;
        }

        if (!ata_read_one_sector((uint32_t)current_lba, &buffer[index * ata_sector_size])) {
            return 0;
        }
    }

    return 1;
}

static int qemu_ata_write(
    void *context,
    uint64_t lba,
    uint32_t sector_count,
    const uint8_t *buffer,
    size_t buffer_size
) {
    (void)buffer_size;

    const struct qemu_ata_context *ata = (const struct qemu_ata_context *)context;

    if (ata == 0 || !ata->present || lba > UINT32_MAX) {
        return 0;
    }

    for (uint32_t index = 0; index < sector_count; index++) {
        const uint64_t current_lba = lba + (uint64_t)index;

        if (current_lba > UINT32_MAX) {
            return 0;
        }

        if (!ata_write_one_sector((uint32_t)current_lba, &buffer[index * ata_sector_size])) {
            return 0;
        }
    }

    return 1;
}

static struct arwill_block_device ata_block_device = {
    .context = &ata_context,
    .name = "qemu ata pio",
    .sector_size = ata_sector_size,
    .sector_count = 0,
    .read = qemu_ata_read,
    .write = qemu_ata_write,
};

const struct arwill_block_device *arwill_qemu_ata_block_device_init(void) {
    uint64_t detected_sector_count = 0;

    ata_context.present = 0;
    ata_context.sector_count = 0;
    ata_block_device.sector_count = 0;

    if (!ata_identify(&detected_sector_count)) {
        return 0;
    }

    ata_context.present = 1;
    ata_context.sector_count = detected_sector_count;
    ata_block_device.sector_count = detected_sector_count;

    return &ata_block_device;
}
