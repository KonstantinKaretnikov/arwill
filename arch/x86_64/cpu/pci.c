#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/io.h>
#include <arwill/arch/x86_64/pci.h>

enum {
    pci_config_address = 0xcf8,
    pci_config_data = 0xcfc,
    pci_max_slots = 32,
    pci_max_functions = 8,
    pci_header_type_register = 0x0c,
    pci_header_multifunction = 0x80,
    pci_vendor_id_missing = 0xffff,
};

static const uint32_t pci_config_enable = 0x80000000U;

static uint32_t config_address(uint8_t slot, uint8_t function, uint8_t offset) {
    return pci_config_enable |
        ((uint32_t)slot << 11U) |
        ((uint32_t)function << 8U) |
        ((uint32_t)offset & 0xfcu);
}

static uint32_t config_read32(uint8_t slot, uint8_t function, uint8_t offset) {
    arwill_x86_64_out32(pci_config_address, config_address(slot, function, offset));
    return arwill_x86_64_in32(pci_config_data);
}

static void record_function(
    struct arwill_pci_bus *bus,
    uint8_t slot,
    uint8_t function
) {
    const uint32_t identity = config_read32(slot, function, 0x00);
    const uint16_t vendor_id = (uint16_t)(identity & 0xffffU);

    if (vendor_id == pci_vendor_id_missing) {
        return;
    }

    if (bus->count >= arwill_pci_device_capacity) {
        bus->truncated = 1;
        return;
    }

    struct arwill_pci_device *device = &bus->devices[bus->count];
    const uint32_t class_register = config_read32(slot, function, 0x08);

    device->bus = 0;
    device->slot = slot;
    device->function = function;
    device->vendor_id = vendor_id;
    device->device_id = (uint16_t)(identity >> 16U);
    device->programming_interface = (uint8_t)((class_register >> 8U) & 0xffU);
    device->subclass = (uint8_t)((class_register >> 16U) & 0xffU);
    device->class_code = (uint8_t)(class_register >> 24U);

    for (size_t index = 0; index < 6U; index++) {
        device->bars[index] = config_read32(slot, function, (uint8_t)(0x10U + index * 4U));
    }

    bus->count++;
}

void arwill_x86_64_pci_scan(struct arwill_pci_bus *bus) {
    arwill_pci_bus_init(bus);

    if (bus == 0) {
        return;
    }

    for (uint8_t slot = 0; slot < pci_max_slots; slot++) {
        const uint32_t header = config_read32(slot, 0, pci_header_type_register);
        const uint8_t function_count =
            (header & pci_header_multifunction) == 0U ? 1U : pci_max_functions;

        for (uint8_t function = 0; function < function_count; function++) {
            record_function(bus, slot, function);
        }
    }
}
