# ADR-0003: x86-64 and QEMU as the First Target

Status: accepted

## Context

The first executable milestone should prove the architecture and development
loop, not solve portability. The required host is macOS, and the requested
execution environment is QEMU.

## Decision

Target x86-64 in QEMU for the first commit. Do not implement ARM, Raspberry Pi,
or a portability layer yet.

## Consequences

The project can use a well-known emulator and a conventional hobby-kernel
target. Architecture-independent kernel code is kept separate from x86-64 and
QEMU code so portability remains a future consequence of clean boundaries, not
a claimed feature.

## Alternatives Considered

Starting with multiple architectures was rejected as premature. Starting with
hardware was rejected because it would complicate the first verification loop.

## Revisit

Revisit after the x86-64/QEMU path has memory management and interrupt
handling boundaries stable enough to compare with another target.
