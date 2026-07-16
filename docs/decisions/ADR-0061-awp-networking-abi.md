# ADR-0061: Bounded AWP Networking ABI

Status: accepted

## Context

ADR-0060 requires a public nonblocking networking ABI after the fixed TCP
endpoint table exists. Arwill has four AWP task slots, four TCP endpoints in
total, and no file-descriptor layer, blocking threads, dynamic allocation, or
POSIX compatibility surface. Endpoint 0 remains owned by the remote console.

## Decision

Add eight `int 0x80` syscalls. Arguments use the existing x86-64 AWP register
convention and results are signed 64-bit values returned in `rax`:

| `rax` | operation | arguments |
| --- | --- | --- |
| 8 | `net_open` | none |
| 9 | `net_bind` | `rdi=handle`, `rsi=local_port` |
| 10 | `net_listen` | `rdi=handle` |
| 11 | `net_connect` | `rdi=handle`, `rsi=IPv4 big-endian value`, `rdx=remote_port` |
| 12 | `net_accept` | `rdi=handle` |
| 13 | `net_read` | `rdi=handle`, `rsi=buffer`, `rdx=capacity` |
| 14 | `net_write` | `rdi=handle`, `rsi=buffer`, `rdx=length` |
| 15 | `net_close` | `rdi=handle` |

Each AWP slot owns handles `0` and `1`. `net_open` reserves one of the three
application TCP endpoints and returns the lowest free handle. A handle must be
bound before `listen` or `connect`. Bind rejects port zero, duplicate local
ports, and ports outside 16 bits.

One endpoint supports one listener and at most one connection. Consequently,
`net_accept` reports readiness by returning the same handle once its passive
handshake is established; it does not allocate a second connection object.
This is deliberate and is not POSIX `accept`.

All operations are nonblocking. The signed results are:

- `>= 0`: success, handle, or byte count as appropriate;
- `-1`: retry after `network-poll` makes progress;
- `-2`: invalid operation, handle, argument, state, or user range;
- `-3`: endpoint or network resources unavailable;
- `-4`: peer closed and no readable bytes remain;
- `-5`: local address already in use.

`net_connect` performs asynchronous ARP resolution for the destination or
gateway, sends SYN after resolution, and returns retry until established.
`net_close` requests FIN and returns retry until the endpoint returns to its
listener baseline; the completing call releases the endpoint and handle.
Task exit, cancellation, fault, and slot reuse force-release all owned handles.

The exposed fixed limits are two handles per AWP slot, a 512-byte receive ring,
an 8192-byte transmit ring, four retained TCP segments of at most 1024 bytes,
four total endpoints, and three application endpoints because endpoint 0 is
reserved for the remote console. A single read or write is capped at 256 bytes
to match the existing AWP bounded-copy convention.

## Consequences

AWP programs can implement real inbound and outbound IPv4 TCP consumers while
the kernel retains fixed memory use and one polling progress owner. The ABI is
small and explicit, but it is neither sockets nor file descriptors. There is
no wildcard bind, backlog, multiple accepted children, DNS, UDP, IPv6, TLS,
blocking wait, readiness set, inherited handle, or handle transfer.

The big-endian IPv4 value is written in normal dotted-quad order; for example,
`10.0.2.2` is `0x0a000202`.

## Verification

Native tests must cover handle and endpoint exhaustion, duplicate bind,
simultaneous ports, retry results, queue pressure, peer close, active SYN,
task cleanup, and invalid user ranges. QEMU smoke must run a stored AWP network
consumer concurrently with both serial and authenticated remote shells.

## Revisit

Revisit separate accepted-child handles or a broader socket-like model only
after a concrete program cannot fit the one-connection endpoint contract.
