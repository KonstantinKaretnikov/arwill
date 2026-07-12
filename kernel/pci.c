#include <arwill/kernel/pci.h>

void arwill_pci_bus_init(struct arwill_pci_bus *bus) {
    if (bus == 0) {
        return;
    }

    bus->count = 0;
    bus->truncated = 0;
}
