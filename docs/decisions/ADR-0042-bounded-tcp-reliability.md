# ADR-0042: Bounded TCP Reliability

Status: accepted; blocking-output portion superseded by ADR-0047

## Context

ADR-0041 replaced unfinished SSH work with a small plaintext TCP remote
console. The localhost QEMU path is useful, but a lost SYN-ACK, output segment,
or ACK can leave the listener stuck because the initial transport retained no
segment and had no time-based recovery. It also accepted packets without
validating their IPv4 or TCP checksums.

A general TCP stack would add windows, congestion control, adaptive timers,
out-of-order queues, a complete close state machine, and a socket API without a
current consumer. The remote console needs a smaller reliability increment.

## Decision

Retain the single-listener polling design and add bounded stop-and-wait
reliability:

- inject the architecture-independent monotonic clock into the IPv4 stack;
- validate the fixed IPv4 header checksum and the TCP pseudo-header checksum;
- answer a repeated SYN with the current SYN-ACK;
- ACK duplicate or out-of-order input again without enqueueing it twice;
- retain at most one unacknowledged SYN-ACK or console-output segment;
- limit retained output payload to 1024 bytes;
- retransmit after a fixed 250 ms interval, at most three times;
- reset the listener to `listen` when the retry budget is exhausted;
- expose checksum-drop, duplicate-ACK, retransmission, timeout, and pending
  diagnostics through `tcpinfo` and `tcplisten`.

The original decision made remote output wait for acknowledgement of the
previous retained segment before sending another. ADR-0047 replaces that wait
with a bounded byte queue while retaining only one retransmittable segment.
Pure ACK and FIN segments are not retained.

## Consequences

The console can recover from a lost SYN-ACK, output segment, or peer ACK and no
longer accepts corrupted IPv4/TCP input. Retry exhaustion releases a stuck
listener for a new connection. The retained-segment buffer remains fixed at
1024 bytes. Remote output no longer blocks after ADR-0047.

This is still not a general or standards-complete TCP implementation. It has no
sliding window, congestion control, adaptive retransmission timeout,
out-of-order reassembly, advertised receive-window management, complete close
states, or socket contract. The 512-byte input queue can still drop bytes if
the shell does not drain it quickly enough.

## Verification

A native host test uses fake network and monotonic-clock devices to verify bad
IPv4 and TCP checksum rejection, delayed retransmission, acknowledgement
clearing, duplicate-data re-ACK without duplicate delivery, retry exhaustion,
and listener recovery. The existing QEMU smoke test continues to verify two
real sequential `nc` sessions and listener reuse.

## Revisit

Add a socket contract, receive windows, adaptive timers, or full close states
only when a second network service or a non-localhost deployment requires
them.
