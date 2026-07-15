# ADR-0060: Bounded TCP v2 Programme

Status: accepted

## Context

Arwill `0.20.3` has one inbound TCP listener coupled directly to the plaintext
remote console. It validates checksums, retains one segment for stop-and-wait
retransmission, and recovers a stuck listener after a fixed retry budget. The
host can now reach the guest directly through UTM macOS Shared networking, so
the transport is no longer exercised only through QEMU localhost forwarding.

The current design has only `listen`, `syn-received`, and `established` states,
one global peer, a fixed advertised window unrelated to receive capacity, and
diagnostics whose transport and service counters are mixed. AWP programs have
no network ABI. Expanding all of this in one replacement would make failures
hard to localize and would violate the project's sequential milestone rule.

## Decision

Evolve TCP through independently bootable and verified increments:

1. Add transport-wide counters, a full local/remote IPv4 and port tuple,
   wrap-safe sequence comparison, RST accounting, and a bounded close state
   machine. Keep the remote console as the only endpoint.
2. Advertise actual receive capacity, negotiate a bounded MSS, reject rather
   than acknowledge bytes that cannot be retained, add adaptive retransmission
   timing with bounded exponential backoff, and retain a small fixed send
   queue.
3. Move connection state and byte queues behind an architecture-independent,
   nonblocking kernel stream contract. The IPv4 layer owns framing and TCP
   transport; the remote-console service owns authentication and shell state.
4. Replace the single endpoint with a fixed table. Each endpoint binds one
   local port and has fixed connection and queue storage. Dispatch uses the
   complete four-tuple. No allocation occurs while receiving a frame.
5. Add a bounded AWP networking ABI only after the kernel contract exists.
   Each of the four existing AWP slots owns a fixed handle table. Operations
   are nonblocking and cover `open`, `bind`, `listen`, `connect`, `accept`,
   `read`, `write`, and `close`; negative results distinguish retryable,
   invalid, unavailable, and closed states. Task exit and fault cleanup close
   every owned handle. The first stored AWP network program is a real consumer
   of the multi-endpoint path, not a kernel-only placeholder service.

Keep the existing remote-console port, access-key gate, plaintext warning,
single-session behavior, bounded output, and remote `exit` semantics throughout
the programme. Keep `network-poll` and `remote-console` as distinct system
tasks. AWP networking progresses through the existing network polling task and
does not add blocking waits inside `int 0x80`.

The initial fixed limits are design inputs, not promises of POSIX scale:

- four TCP endpoints and four live connections system-wide;
- two network handles per AWP slot;
- fixed receive and transmit byte rings per connection;
- a fixed retransmission queue whose memory is allocated at boot;
- IPv4 only, with the existing fixed address and gateway configuration.

Exact queue sizes and syscall register layouts must be recorded in the
milestone that first exposes them and covered by compile-time bounds and native
tests.

## Consequences

TCP becomes reusable by more than the remote console without introducing a
general allocator, file descriptors, inherited handles, blocking kernel
threads, or a POSIX compatibility claim. Transport-wide diagnostics can count
traffic for unknown ports while endpoint diagnostics remain service-specific.

The programme is intentionally not a complete Internet TCP implementation.
TLS, DNS, IPv6, IP fragmentation, SACK, urgent data, dynamic socket counts,
SMP, and unrestricted congestion windows remain absent. Until a bounded
congestion-control milestone is accepted, the send flight remains deliberately
small and the supported deployment remains a trusted local or VM network.

The AWP ABI is a public contract once introduced. Its syscall numbers, handle
ownership, return values, cancellation behavior, and buffer validation cannot
change silently.

## Verification

Each increment must retain all existing host and QEMU checks and add focused
native packet tests. The combined programme must cover checksum failures,
unknown ports, SYN/FIN/RST loss, sequence wraparound, duplicate and reordered
segments, zero-window recovery, retry exhaustion, queue pressure, simultaneous
ports, task cleanup, and invalid user pointers. QEMU and UTM Shared networking
must both exercise the remote console after the refactor. The AWP milestone
must run its stored network consumer concurrently with the serial and remote
shells.

## Revisit

Consider dynamic sockets, broader congestion control, DNS, TLS, or IPv6 only
after the bounded API has a concrete consumer that cannot be served within
these fixed limits.
