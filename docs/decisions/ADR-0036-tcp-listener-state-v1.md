# ADR-0036: TCP Listener State v1

Status: accepted

## Context

After verified ARP/IPv4/ICMP, the next SSH prerequisite is TCP connection
state. A full socket layer and packet integration would be too large for one
increment.

## Decision

Add a small architecture-independent TCP listener state machine. It accepts a
SYN for a configured port, produces SYN-ACK values, validates the final ACK,
and exposes `listen`, `syn-received`, and `established` states. The first
consumer is a `tcpcheck` shell diagnostic for port 22.

## Consequences

TCP handshake state is now explicit and testable. It is not yet wired to IPv4
TCP packet parsing, checksums, retransmission, payload delivery, or a socket
API; those are the next part of the same milestone.

## Verification

QEMU smoke runs `tcpcheck` and observes `listener state established`.
