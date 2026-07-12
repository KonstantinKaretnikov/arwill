#ifndef ARWILL_PLATFORM_QEMU_E1000_H
#define ARWILL_PLATFORM_QEMU_E1000_H

#include <stdint.h>

#include <arwill/kernel/memory.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/pci.h>

const struct arwill_network_device *arwill_qemu_e1000_init(
    const struct arwill_pci_bus *pci,
    struct arwill_memory *memory,
    uint64_t hhdm_offset
);

#endif
