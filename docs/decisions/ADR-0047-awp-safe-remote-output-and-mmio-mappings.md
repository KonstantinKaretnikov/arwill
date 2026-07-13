# ADR-0047: AWP-Safe Remote Output and MMIO Mappings

Status: accepted

## Context

The first bounded TCP reliability path waited for the previous output ACK
inside `arwill_ipv4_remote_console_write`. Once AWP programs could write to a
TCP-backed console, that wait could occur inside the `int 0x80` handler, where
timer interrupts are not a valid progress mechanism. The same end-to-end test
also exposed that e1000 MMIO was mapped after AWP address spaces copied the
kernel PML4. A remote AWP could therefore enter kernel code under a CR3 that did
not contain the supervisor-only device mapping.

## Decision

Keep exactly one retained TCP segment for retransmission, but place console
bytes in a fixed 8192-byte transmit ring. A console write only enqueues bytes
and may send the first segment; it never waits for a peer ACK. The polling path
advances queued output after acknowledgements. On overflow or disconnect,
count discarded bytes in the existing diagnostics.

Map QEMU e1000 MMIO in a dedicated high-half virtual range, separate from the
Limine HHDM. Initialize that platform mapping before creating the four AWP
address spaces so their copied kernel PML4 entries include all current
supervisor-only device mappings. The mapping remains inaccessible from ring 3.

Drain one remote input byte per shell loop, matching serial-session fairness.
When a peer FIN is observed, process already queued input before closing the
session.

## Consequences

AWP output cannot freeze the kernel while waiting for TCP progress, and kernel
device access remains valid during a syscall under any AWP CR3. Output remains
bounded and can be dropped if a peer cannot drain the fixed queue. This does
not add asynchronous sockets, dynamic VM mappings, per-process kernel stacks,
or a general network buffer framework.

Future platform MMIO mappings added after AWP runtime initialization will need
an explicit kernel-mapping propagation mechanism or the same initialization
ordering.

## Verification

Run the editor in the serial session while an authenticated TCP session runs
the calculator. Verify both accept input, the calculator returns `9*9=81`, the
editor persists text, both tasks exit independently, and the kernel remains
responsive. The QEMU smoke test covers this path.
