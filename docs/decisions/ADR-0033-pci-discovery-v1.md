# ADR-0033: PCI Discovery v1

Status: accepted

## Context

The planned QEMU network path needs a real discoverable PCI NIC. Arwill has no
bus hierarchy or dynamic driver model, so the first step must remain a bounded
inspection contract.

## Decision

Add an architecture-independent fixed PCI device table and an x86-64
implementation using PCI configuration mechanism #1 (`0xcf8`/`0xcfc`). The
first scan covers bus 0, up to 32 slots, and up to 8 functions for
multifunction devices. It records vendor/device IDs, class information, and
the six raw BAR values.

QEMU smoke and run paths attach an Intel e1000 device. The shell exposes
`pciinfo` for inspection.

## Consequences

PCI devices are now real input to future platform drivers. The table is fixed
at 32 entries and does not provide BAR mapping, interrupt routing, MSI/MSI-X,
hotplug, resource allocation, or driver binding.

## Verification

The QEMU smoke test observes an e1000 with vendor `8086` and device `100e`.
