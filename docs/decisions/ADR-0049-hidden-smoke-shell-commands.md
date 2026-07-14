# ADR-0049: Hidden Smoke Shell Commands

Status: accepted

## Context

Several early milestones exposed direct shell probes so the QEMU smoke path
could verify a new subsystem before it had a real consumer. The resulting
commands remained in `help` and Tab completion after the user-facing system
grew higher-level inspection commands, an automatic remote-console service,
preemptive stored AWP programs, and `/apps/edit.awp`.

The smoke path still needs the probes. It also uses direct whole-file text and
binary writes to build deterministic filesystem and AWP fixtures. Removing the
dispatch would either lose coverage or require a new test-only kernel API with
no current use case.

## Decision

Keep these exact shell inputs for internal smoke use:

- network probes: `netprobe`, `arping`, `tcpcheck`, and `tcplisten`;
- kernel probes: `heaptest` and `irqprobe`;
- cooperative built-in continuation: `step`;
- deterministic filesystem fixture writes: `write` and `writehex`.

Do not include these names in the canonical command table used by `help` and
Tab completion. Do not document them as user commands. The dispatcher retains
their exact implementations, and the QEMU smoke script enters their complete
names without completion.

The user-facing alternatives are the existing inspection commands, automatic
remote-console service, preemptive AWP runtime, and `/apps/edit.awp` for ASCII
text. The editor does not replace binary fixture creation; `writehex` remains
internal for that reason.

This decision supersedes only the user-visible shell-exposure portions of
ADR-0019, ADR-0023, ADR-0024, ADR-0030, ADR-0034, ADR-0035, ADR-0036, and
ADR-0042. Their subsystem contracts and verification requirements remain in
force.

## Consequences

`help` remains the authoritative user command list, and Tab completion no
longer advertises engineering probes or direct fixture writers. A person who
knows an internal command can still type it exactly; this is discoverability
control, not a security boundary.

The smoke suite continues to exercise the original subsystem paths without a
build flag, alias command, cross-layer test hook, or second shell dispatcher.
Future user-facing mutation workflows should go through applications or a new
documented contract rather than exposing these fixture commands again.

## Verification

Build the kernel and run native checks plus the bounded QEMU smoke path. Verify
that `help` omits every internal name while exact internal inputs still produce
their expected diagnostic and fixture results across reboot.
