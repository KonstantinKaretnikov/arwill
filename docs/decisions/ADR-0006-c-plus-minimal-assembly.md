# ADR-0006: C Plus Minimal Assembly for the Initial Implementation

Status: accepted

## Context

The initial implementation needs freestanding kernel code and a small amount of
x86-64 CPU and port I/O behavior. The project should remain easy to read and
review.

## Decision

Use freestanding C for Arwill-owned kernel and platform code, with isolated
x86-64 inline assembly only for port I/O and CPU halt behavior.

## Consequences

Most code is ordinary C compiled without the host C standard library. Assembly
is kept at the architecture boundary. The project does not introduce NASM until
there is a current need for a standalone assembly file.

## Alternatives Considered

More assembly was rejected because Limine already handles early boot setup.
Using C++ or Rust was rejected for the first commit because the requested scope
is C and minimal assembly, and those languages would add runtime and tooling
questions too early.

## Revisit

Revisit if a future architecture boundary is clearer in a standalone assembly
file, or if the project chooses a language with stronger safety properties for
larger kernel subsystems.
