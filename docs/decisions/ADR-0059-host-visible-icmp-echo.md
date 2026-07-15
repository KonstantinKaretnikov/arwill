# ADR-0059: Host-Visible ICMP Echo Replies

Status: accepted

## Context

Arwill can send one diagnostic ICMP echo request to its fixed gateway, and its
continuous network task handles ARP and remote-console TCP frames. It does not
answer an echo request received from the host because the continuous poll path
drops all inbound IPv4 protocols other than TCP.

UTM can place the guest and macOS host on a shared network where they can see
each other. Arwill does not implement DHCP, so that network must match its
existing fixed `10.0.2.15/24` address and `10.0.2.2` gateway.

## Decision

Extend the architecture-independent bounded IPv4 poll path to recognize ICMP
echo requests addressed to Arwill. Accept only IPv4 packets with a valid
variable-length header checksum, a valid ICMP checksum, echo type and code,
and no fragmentation. Reply directly to the source Ethernet and IPv4 addresses
while preserving the request identifier, sequence, and payload.

Keep the existing fixed address. Document UTM's `macOS Shared` mode with guest
network `10.0.2.0/24`, host address `10.0.2.2`, e1000 emulation, and guest
isolation disabled. Do not use `Emulated VLAN` port forwarding for this path,
because that interface forwards TCP and UDP rather than ICMP.

## Consequences

The macOS host can use its ordinary `ping 10.0.2.15` command once UTM and
Arwill share the documented subnet. Echo handling progresses through the
existing cooperative `network-poll` system task and remains independent of the
remote-console service state.

This does not add DHCP, runtime network configuration, routing, fragmentation
reassembly, ICMP errors, rate limiting, or a general socket or ICMP API. UTM
configuration remains a manual verification target.

## Alternatives Considered

UDP or TCP port forwarding would not satisfy ordinary host ICMP ping. Adding
DHCP would remove the need for a fixed UTM subnet, but would substantially
expand configuration and protocol scope for this increment. Bridging to the
physical LAN would also require an address matching that LAN and introduces
Wi-Fi-specific host configuration.

## Revisit

Revisit the fixed address when Arwill gains a deliberate runtime IPv4
configuration or DHCP milestone. Revisit ICMP rate limiting before exposing
the guest beyond a trusted host or LAN.
