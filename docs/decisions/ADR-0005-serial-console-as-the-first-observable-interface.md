# ADR-0005: Serial Console as the First Observable Interface

Status: accepted

## Context

The first milestone needs observable output that does not depend on graphics,
keyboard input, a framebuffer terminal, or a shell.

## Decision

Use QEMU serial output through the emulated x86-64 COM1 port as the first
observable interface.

## Consequences

The smoke test can capture host-side serial output non-interactively. The
console contract stays small and text-only. Graphics, keyboard input, terminal
handling, and formatting are intentionally absent.

## Alternatives Considered

Framebuffer output was rejected because it would add boot protocol requests and
pixel rendering before the architecture needs them. Port `0xe9` debug output
was rejected because COM1 is a more conventional serial-console path.

## Revisit

Revisit when the kernel needs interactive diagnostics, framebuffer output, or a
second logging destination.
