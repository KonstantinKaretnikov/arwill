#ifndef ARWILL_PLATFORM_QEMU_ATA_PIO_H
#define ARWILL_PLATFORM_QEMU_ATA_PIO_H

#include <arwill/kernel/block_device.h>

const struct arwill_block_device *arwill_qemu_ata_block_device_init(void);

#endif
