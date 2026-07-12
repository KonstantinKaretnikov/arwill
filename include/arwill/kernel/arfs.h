#ifndef ARWILL_KERNEL_ARFS_H
#define ARWILL_KERNEL_ARFS_H

#include <arwill/kernel/block_device.h>
#include <arwill/kernel/filesystem.h>

const struct arwill_filesystem *arwill_arfs_mount(
    const struct arwill_block_device *block_device
);

#endif
