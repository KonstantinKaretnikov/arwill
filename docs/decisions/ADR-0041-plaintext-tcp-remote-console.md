# ADR-0041: Plaintext TCP Remote Console

Status: superseded by ADR-0042

## Context

The incremental SSH path had grown into a substantial cryptographic and
protocol subsystem before Arwill could provide an interactive remote shell.
The project owner chose a smaller current requirement: control a QEMU guest
from `nc` without carrying unfinished SSH code or cryptographic dependencies.

Arwill already has a bounded e1000, ARP, IPv4, and single-listener TCP path.
Those layers are useful for a simple remote console and remain independent of
SSH.

## Decision

Supersede ADR-0036 through ADR-0040 as active direction. Remove the SSH
transport, host-key persistence, entropy contract, crypto contract, and all
imported BearSSL subsets. Restore the ARFS v2 entry limits that existed before
the host-key file.

Retain the common network layers and serve one plaintext remote console on
guest TCP port `2323`. QEMU `make run` forwards host localhost port `23232` by
default. Serial and remote connections keep independent line, history, and
current-directory state while using the same canonical shell command
dispatcher. Backspace, command completion, history, Ctrl+C line cancellation,
and CR/LF input are handled by the shared session code. Remote `exit` sends a
close and returns the listener to `listen`; serial `exit` continues to power
off the machine.

Document a direct host-side `stty raw -echo; nc ...; stty <saved-state>`
invocation. Raw, no-echo mode is required for arrows, Tab, and Ctrl+C to cross
the host terminal immediately instead of being consumed or buffered by its
canonical mode. Do not retain a separate client wrapper: the remote console
protocol remains directly compatible with `nc`.

The shell loop polls the service directly. The remote console is not modeled
as a process because Arwill still has no saved execution contexts or general
socket API.

## Consequences

Arwill now provides useful interactive remote control with a much smaller code
surface and no cryptographic dependency. The interface has no authentication
or confidentiality and must remain bound to localhost or another explicitly
trusted test environment. It is not Telnet and performs no Telnet option
negotiation.

The TCP implementation still has one connection, bounded receive buffering,
no retransmission, no inbound checksum validation, and only a minimal close
path. Interactive AWP programs still use the serial-oriented syscall input and
are not made network-native by this decision.

## Alternatives Considered

Completing SSH would provide secure standard access but requires substantially
more audited cryptography, transport state, authentication, channels, and TCP
reliability. Minimal Telnet would still add option negotiation without solving
security. A plain line protocol compatible with `nc` meets the current need.

## Verification

The QEMU smoke test connects twice through a real localhost port forward. It
checks the banner, shared command execution, Backspace correction, Up/Down
history, Ctrl+C line cancellation, remote `exit`, and listener reuse while preserving the full
serial boot, filesystem persistence, AWP, and poweroff checks.

## Revisit

Reconsider a secure remote protocol only when Arwill has a general socket
contract, reliable TCP behavior, and a concrete requirement to expose remote
control beyond a trusted local development host.
