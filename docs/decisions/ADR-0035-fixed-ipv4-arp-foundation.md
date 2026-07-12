# ADR-0035: Fixed IPv4 and ARP Foundation

Status: accepted

## Context

The Ethernet frame path is usable, but a future SSH test needs a small network
layer above raw frames. Arwill does not yet have configuration storage, DHCP,
or sockets.

## Decision

Add an architecture-independent fixed IPv4 stack contract for the QEMU user
network defaults: guest `10.0.2.15/24` and gateway `10.0.2.2`. The first
protocol operation is an Ethernet/ARP request for the gateway, exposed through
`arping`; `netcfg` reports the fixed values.

There is no DHCP, persistent configuration, ARP cache, reply wait, IPv4 packet
parser, ICMP, TCP, or socket API yet. The fixed values are deliberately
diagnostic scaffolding for the next protocol milestone.

## Consequences

Protocol frame construction stays in the kernel networking layer and hardware
access remains behind the network-device contract. The QEMU platform remains
the only supported network environment.

## Verification

The QEMU smoke test observes `netcfg` and a successfully transmitted
`arping` request to `10.0.2.2`.
