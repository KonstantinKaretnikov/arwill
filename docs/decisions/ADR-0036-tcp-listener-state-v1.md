# ADR-0036: TCP Listener State v1

Status: accepted

## Context

After verified ARP/IPv4/ICMP, the next SSH prerequisite is TCP connection
state. A full socket layer and packet integration would be too large for one
increment.

## Decision

Add a small architecture-independent TCP listener state machine. It accepts a
SYN for a configured port, produces SYN-ACK values, validates the final ACK,
and exposes `listen`, `syn-received`, and `established` states. The IPv4 layer
now parses inbound TCP headers for port 22 and emits a TCP SYN-ACK frame. The
shell exposes `tcpcheck` and bounded `tcplisten` diagnostics.

## Consequences

TCP handshake state is now explicit and wired through Ethernet/IPv4 framing.
TCP checksums are emitted for SYN-ACK. Retransmission, validation of incoming
checksums, payload delivery, a persistent service loop, and a socket API are
the next part of the same milestone.

## Verification

QEMU smoke runs `tcpcheck` and observes `listener state established`.
