#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/memory.h>
#include <arwill/kernel/network.h>
#include <arwill/kernel/pci.h>
#include <arwill/arch/x86_64/pci.h>
#include <arwill/arch/x86_64/io.h>
#include <arwill/platform/qemu/e1000.h>

enum {
    e1000_vendor_id = 0x8086,
    e1000_device_id = 0x100e,
    e1000_bar0 = 0,
    e1000_register_ctrl = 0x0000,
    e1000_register_status = 0x0008,
    e1000_register_ral = 0x5400,
    e1000_register_rah = 0x5404,
    e1000_register_tdbal = 0x3800,
    e1000_register_tdbah = 0x3804,
    e1000_register_tdlen = 0x3808,
    e1000_register_tdh = 0x3810,
    e1000_register_tdt = 0x3818,
    e1000_register_rdbal = 0x2800,
    e1000_register_rdbah = 0x2804,
    e1000_register_rdlen = 0x2808,
    e1000_register_rdh = 0x2810,
    e1000_register_rdt = 0x2818,
    e1000_register_rctl = 0x0100,
    e1000_register_tctl = 0x0400,
    e1000_register_tipg = 0x0410,
    e1000_register_ims = 0x00d0,
    e1000_ctrl_reset = 1U << 26U,
    e1000_rctl_enable = 1U << 1U,
    e1000_rctl_bam = 1U << 15U,
    e1000_rctl_secrc = 1U << 26U,
    e1000_tctl_enable = 1U << 1U,
    e1000_tctl_pad = 1U << 3U,
    e1000_tx_command_eop = 1U,
    e1000_tx_command_ifcs = 1U << 1U,
    e1000_tx_command_rs = 1U << 3U,
    e1000_tx_status_dd = 1U,
    e1000_rx_status_dd = 1U,
    e1000_rx_status_eop = 1U << 1U,
    e1000_descriptor_alignment = 16,
    e1000_ring_count = 8,
    e1000_buffer_capacity = 2048,
    e1000_poll_limit = 100000,
};

static const uint64_t e1000_mmio_virtual_base = 0xffffc00000000000ULL;

struct e1000_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum;
    uint16_t special;
} __attribute__((packed, aligned(e1000_descriptor_alignment)));

struct e1000_rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed, aligned(e1000_descriptor_alignment)));

struct e1000_context {
    struct arwill_memory *memory;
    uint64_t hhdm_offset;
    volatile uint8_t *registers;
    uint16_t io_base;
    int io_mode;
    volatile struct e1000_descriptor *tx_descriptor;
    volatile struct e1000_rx_descriptor *rx_descriptor;
    uint64_t tx_descriptor_physical;
    uint64_t rx_descriptor_physical;
    uint64_t tx_buffer_physical[e1000_ring_count];
    uint64_t rx_buffer_physical[e1000_ring_count];
    uint8_t *tx_buffer[e1000_ring_count];
    uint8_t *rx_buffer[e1000_ring_count];
    uint32_t tx_tail;
    uint8_t mac[arwill_network_mac_length];
    int ready;
};

static struct e1000_context e1000;
static const uint8_t e1000_configured_mac[arwill_network_mac_length] = {
    0x52, 0x54, 0x00, 0x12, 0x34, 0x56
};

static uint8_t *physical_to_virtual(uint64_t offset, uint64_t physical) {
    return (uint8_t *)(uintptr_t)(offset + physical);
}

static uint64_t read_cr3(void) {
    uint64_t value = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static size_t page_table_index(uint64_t virtual_address, unsigned shift) {
    return (size_t)((virtual_address >> shift) & 0x1ffULL);
}

static int ensure_table(uint64_t *table, size_t index, struct arwill_memory *memory,
    uint64_t hhdm_offset, uint64_t **child) {
    uint64_t entry = table[index];
    if ((entry & 1U) == 0U) {
        uint64_t physical = 0;
        if (!arwill_physical_allocate_page(memory, &physical)) {
            return 0;
        }
        uint8_t *page = physical_to_virtual(hhdm_offset, physical);
        for (size_t offset = 0; offset < ARWILL_MEMORY_PAGE_SIZE; offset++) {
            page[offset] = 0;
        }
        entry = physical | 0x3U;
        table[index] = entry;
    } else if ((entry & (1ULL << 7U)) != 0U) {
        return 0;
    }
    *child = (uint64_t *)physical_to_virtual(hhdm_offset, entry & 0x000ffffffffff000ULL);
    return 1;
}

static int map_mmio_page(struct arwill_memory *memory, uint64_t hhdm_offset,
    uint64_t virtual_address, uint64_t physical_address) {
    uint64_t *pml4 = (uint64_t *)physical_to_virtual(hhdm_offset, read_cr3() & 0x000ffffffffff000ULL);
    uint64_t *pdpt = 0;
    uint64_t *pd = 0;
    uint64_t *pt = 0;
    if (!ensure_table(pml4, page_table_index(virtual_address, 39U), memory, hhdm_offset, &pdpt) ||
        !ensure_table(pdpt, page_table_index(virtual_address, 30U), memory, hhdm_offset, &pd) ||
        !ensure_table(pd, page_table_index(virtual_address, 21U), memory, hhdm_offset, &pt)) {
        return 0;
    }
    pt[page_table_index(virtual_address, 12U)] =
        (physical_address & 0x000ffffffffff000ULL) | 0x1bU;
    invalidate_page(virtual_address);
    return 1;
}

static uint32_t register_read(uint32_t offset) {
    if (e1000.io_mode) {
        arwill_x86_64_out32(e1000.io_base, offset);
        return arwill_x86_64_in32((uint16_t)(e1000.io_base + 4U));
    }
    return *(volatile uint32_t *)(uintptr_t)(e1000.registers + offset);
}

static void register_write(uint32_t offset, uint32_t value) {
    if (e1000.io_mode) {
        arwill_x86_64_out32(e1000.io_base, offset);
        arwill_x86_64_out32((uint16_t)(e1000.io_base + 4U), value);
        return;
    }
    *(volatile uint32_t *)(uintptr_t)(e1000.registers + offset) = value;
}

static int allocate_page(uint64_t *physical, uint8_t **virtual) {
    if (!arwill_physical_allocate_page(e1000.memory, physical)) {
        return 0;
    }

    *virtual = physical_to_virtual(e1000.hhdm_offset, *physical);
    for (size_t index = 0; index < ARWILL_MEMORY_PAGE_SIZE; index++) {
        (*virtual)[index] = 0;
    }
    return 1;
}

static const struct arwill_pci_device *find_device(const struct arwill_pci_bus *pci) {
    if (pci == 0) {
        return 0;
    }

    for (size_t index = 0; index < pci->count; index++) {
        const struct arwill_pci_device *device = &pci->devices[index];
        if (device->vendor_id == e1000_vendor_id && device->device_id == e1000_device_id) {
            return device;
        }
    }
    return 0;
}

static int read_mac(void *context, uint8_t mac[arwill_network_mac_length]) {
    const struct e1000_context *device = (const struct e1000_context *)context;
    if (device == 0 || !device->ready || mac == 0) {
        return 0;
    }

    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        mac[index] = device->mac[index];
    }
    return 1;
}

static int send_frame(void *context, const uint8_t *frame, size_t length) {
    struct e1000_context *device = (struct e1000_context *)context;
    if (device == 0 || !device->ready || frame == 0 || length == 0U ||
        length > e1000_buffer_capacity ||
        device->tx_descriptor[device->tx_tail].status != e1000_tx_status_dd) {
        return 0;
    }

    const uint32_t descriptor_index = device->tx_tail;
    for (size_t index = 0; index < length; index++) {
        device->tx_buffer[descriptor_index][index] = frame[index];
    }
    device->tx_descriptor[descriptor_index].length = (uint16_t)length;
    device->tx_descriptor[descriptor_index].command =
        e1000_tx_command_eop | e1000_tx_command_ifcs | e1000_tx_command_rs;
    device->tx_descriptor[descriptor_index].status = 0;
    device->tx_tail = (descriptor_index + 1U) % e1000_ring_count;
    register_write(e1000_register_tdt, device->tx_tail);

    for (size_t index = 0; index < e1000_poll_limit; index++) {
        if ((device->tx_descriptor->status & e1000_tx_status_dd) != 0U) {
            return 1;
        }
    }
    return 0;
}

static int poll_frame(
    void *context,
    uint8_t *frame,
    size_t capacity,
    size_t *length
) {
    struct e1000_context *device = (struct e1000_context *)context;
    if (device == 0 || !device->ready || frame == 0 || length == 0 || capacity == 0U) {
        return 0;
    }

    if ((device->rx_descriptor->status & e1000_rx_status_dd) == 0U) {
        return 0;
    }

    const size_t received = device->rx_descriptor->length;
    if ((device->rx_descriptor->status & e1000_rx_status_eop) == 0U ||
        received > capacity || received > e1000_buffer_capacity) {
        device->rx_descriptor->status = 0;
        register_write(e1000_register_rdt, 0U);
        return 0;
    }

    for (size_t index = 0; index < received; index++) {
        frame[index] = device->rx_buffer[0][index];
    }
    *length = received;
    device->rx_descriptor->status = 0;
    register_write(e1000_register_rdt, 0U);
    return 1;
}

static const struct arwill_network_device network_device = {
    .name = "qemu e1000",
    .context = &e1000,
    .send_frame = send_frame,
    .poll_frame = poll_frame,
    .read_mac = read_mac,
};

const struct arwill_network_device *arwill_qemu_e1000_init(
    const struct arwill_pci_bus *pci,
    struct arwill_memory *memory,
    uint64_t hhdm_offset
) {
    const struct arwill_pci_device *pci_device = find_device(pci);
    uint64_t bar = 0;
    uint8_t *virtual = 0;

    e1000.ready = 0;
    e1000.memory = memory;
    e1000.hhdm_offset = hhdm_offset;
    if (pci_device == 0 || memory == 0 || hhdm_offset == 0U) {
        return 0;
    }

    if (!arwill_x86_64_pci_enable_bus_master(pci_device)) {
        return 0;
    }

    if ((pci_device->bars[e1000_bar0] & 1U) != 0U) {
        return 0;
    }
    bar = (uint64_t)(pci_device->bars[e1000_bar0] & 0xfffffff0U);
    if ((pci_device->bars[e1000_bar0] & 0x6U) == 0x4U) {
        bar |= (uint64_t)pci_device->bars[1] << 32U;
    }
    for (size_t page = 0; page < 6U; page++) {
        if (!map_mmio_page(memory, hhdm_offset,
                e1000_mmio_virtual_base + page * ARWILL_MEMORY_PAGE_SIZE,
                (bar & ~0xfffULL) + page * ARWILL_MEMORY_PAGE_SIZE)) {
            return 0;
        }
    }
    e1000.registers = (volatile uint8_t *)(uintptr_t)(e1000_mmio_virtual_base + (bar & 0xfffULL));
    register_write(e1000_register_ctrl, register_read(e1000_register_ctrl) | e1000_ctrl_reset);
    for (size_t index = 0; index < e1000_poll_limit; index++) {
        (void)register_read(e1000_register_status);
    }

    for (size_t index = 0; index < arwill_network_mac_length; index++) {
        e1000.mac[index] = e1000_configured_mac[index];
    }
    register_write(e1000_register_ral,
        (uint32_t)e1000.mac[0] |
        ((uint32_t)e1000.mac[1] << 8U) |
        ((uint32_t)e1000.mac[2] << 16U) |
        ((uint32_t)e1000.mac[3] << 24U));
    register_write(e1000_register_rah,
        (uint32_t)e1000.mac[4] |
        ((uint32_t)e1000.mac[5] << 8U) |
        (1U << 31U));

    if (!allocate_page(&e1000.tx_descriptor_physical, &virtual)) {
        return 0;
    }
    e1000.tx_descriptor = (volatile struct e1000_descriptor *)virtual;
    if (!allocate_page(&e1000.rx_descriptor_physical, &virtual)) {
        return 0;
    }
    e1000.rx_descriptor = (volatile struct e1000_rx_descriptor *)virtual;
    for (size_t index = 0; index < e1000_ring_count; index++) {
        if (!allocate_page(&e1000.tx_buffer_physical[index], &e1000.tx_buffer[index]) ||
            !allocate_page(&e1000.rx_buffer_physical[index], &e1000.rx_buffer[index])) {
            return 0;
        }
        e1000.tx_descriptor[index].address = e1000.tx_buffer_physical[index];
        e1000.tx_descriptor[index].status = e1000_tx_status_dd;
        e1000.rx_descriptor[index].address = e1000.rx_buffer_physical[index];
    }
    register_write(e1000_register_tdbal, (uint32_t)e1000.tx_descriptor_physical);
    register_write(e1000_register_tdbah, (uint32_t)(e1000.tx_descriptor_physical >> 32U));
    register_write(e1000_register_tdlen, e1000_ring_count * sizeof(struct e1000_descriptor));
    register_write(e1000_register_tdh, 0U);
    register_write(e1000_register_tdt, 0U);
    register_write(e1000_register_rdbal, (uint32_t)e1000.rx_descriptor_physical);
    register_write(e1000_register_rdbah, (uint32_t)(e1000.rx_descriptor_physical >> 32U));
    register_write(e1000_register_rdlen, e1000_ring_count * sizeof(struct e1000_descriptor));
    register_write(e1000_register_rdh, 0U);
    register_write(e1000_register_rdt, e1000_ring_count - 1U);
    register_write(e1000_register_rctl, e1000_rctl_enable | e1000_rctl_bam | e1000_rctl_secrc);
    register_write(e1000_register_rdt, e1000_ring_count - 1U);
    register_write(e1000_register_tipg, 10U | (8U << 10U) | (6U << 20U));
    register_write(e1000_register_tctl, e1000_tctl_enable | e1000_tctl_pad | (0x10U << 4U) | (0x40U << 12U));
    register_write(e1000_register_ims, 0U);
    e1000.ready = 1;
    return &network_device;
}
