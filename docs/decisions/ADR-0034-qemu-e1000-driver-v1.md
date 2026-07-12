# ADR-0034: QEMU e1000 Driver v1

Status: accepted

## Context

PCI discovery identifies the QEMU Intel e1000, but a future IP stack needs a
bounded way to exchange Ethernet frames without depending on a full driver
framework.

## Decision

Add an architecture-independent network-device contract with bounded frame
send, frame polling, and MAC-address operations. Add a QEMU platform driver for
the e1000 legacy device. It enables PCI bus mastering, maps the e1000 MMIO BAR
into a small kernel mapping, and uses fixed eight-entry TX/RX rings backed by
kernel pages. The shell exposes `netinfo` and `netprobe`; the latter sends a
60-byte broadcast diagnostic frame.

The driver uses the QEMU-configured deterministic MAC
`52:54:00:12:34:56`. RX is bounded polling only; there are no interrupts,
queues, link negotiation APIs, sockets, or IP configuration yet.

## Consequences

The kernel now has a real platform frame path while keeping the contract
replaceable. The MMIO mapping is intentionally tiny and fixed, and the driver
is QEMU/e1000-specific. The next milestone can build ARP and IPv4 on top of
the frame contract without moving hardware code into the kernel networking
layer.

## Verification

The QEMU smoke test observes the e1000 device, its MAC address, and a
successful `netprobe: transmitted 60 bytes` result.
