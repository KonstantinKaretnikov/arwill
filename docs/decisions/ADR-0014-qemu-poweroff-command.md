# ADR-0014: QEMU Poweroff Command

Status: accepted

## Context

The shell has `halt`, but `halt` intentionally parks the CPU in an idle loop and
does not close the QEMU process. During interactive development, the owner wants
an `exit` command that powers off the current OS session.

Arwill does not yet parse ACPI tables or implement real machine power
management.

## Decision

Add a narrow kernel power contract with one `poweroff` operation. Add `exit` to
the shell. The command writes `status: powering off` and calls the power
contract. `halt` remains the explicit CPU idle command.

For the first QEMU target, implement poweroff through QEMU's `isa-debug-exit`
device on I/O port `0xf4`. Host-side `make run` and the smoke test add:

```text
-device isa-debug-exit,iobase=0xf4,iosize=0x04
```

The guest writes the 32-bit value `0x10` to the device. QEMU exits with status
`33`, which host tooling treats as a successful Arwill-requested poweroff.

## Consequences

Interactive sessions can now end from inside Arwill with `exit`.

This is a QEMU development poweroff path, not general ACPI shutdown. The power
contract keeps the shell and architecture-independent kernel code separate from
the QEMU-specific I/O port. If the platform poweroff implementation returns, the
kernel falls back to the CPU idle loop.

## Alternatives Considered

Using `halt` as `exit` was rejected because `halt` already has a precise CPU
idle meaning. ACPI S5 shutdown was deferred because it requires ACPI discovery
and table parsing that Arwill does not yet have. Calling QEMU-specific port I/O
directly from the shell was rejected because it would cross architecture and
platform boundaries.

## Revisit

Revisit when Arwill adds ACPI table parsing, a real power-management subsystem,
or support for non-QEMU platforms.
