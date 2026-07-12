#ifndef ARWILL_KERNEL_PCI_H
#define ARWILL_KERNEL_PCI_H

#include <stddef.h>
#include <stdint.h>

enum { arwill_pci_device_capacity = 32 };

struct arwill_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint32_t bars[6];
};

struct arwill_pci_bus {
    struct arwill_pci_device devices[arwill_pci_device_capacity];
    size_t count;
    int truncated;
};

void arwill_pci_bus_init(struct arwill_pci_bus *bus);

#endif
