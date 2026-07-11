# ADR-0002: Replaceable Blocks With Explicit Contracts

Status: accepted

## Context

Arwill must be built from components that can be understood and replaced
without rewriting unrelated subsystems. The first milestone needs only a few
blocks, but their boundaries set the precedent.

## Decision

Use replaceable blocks with explicit public contracts where there is a real
architectural responsibility. For 0.0.1, the blocks are kernel startup, console,
QEMU serial console, and CPU idle.

## Consequences

The kernel entry depends on the console and CPU idle contracts, not on COM1
registers or inline `hlt`. The serial implementation is concrete and small.
No generic driver model, service locator, plugin system, or broad future-facing
interface is introduced.

## Alternatives Considered

A direct `serial_write` call from `kernel/main.c` was rejected because it would
couple kernel orchestration to the current platform device. A full device
framework was rejected because there is only one device and no current need for
enumeration or dynamic binding.

## Revisit

Revisit when a second console implementation, logging sink, or platform target
exists. That evidence may justify a slightly richer contract.
