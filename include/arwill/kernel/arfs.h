#ifndef ARWILL_KERNEL_ARFS_H
#define ARWILL_KERNEL_ARFS_H

struct arwill_block_device;
struct arwill_filesystem;

const struct arwill_filesystem *arwill_arfs_mount(
    const struct arwill_block_device *block_device
);

#endif
