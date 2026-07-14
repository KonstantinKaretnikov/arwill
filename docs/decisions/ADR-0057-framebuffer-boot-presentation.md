# ADR-0057: Framebuffer Boot Presentation

Status: accepted

## Context

The framebuffer text console makes Arwill visible outside the serial terminal,
but presenting every boot byte identically leaves the local boot screen looking
like a diagnostic log. A clearer identity and readiness indication is useful
for QEMU and UTM without justifying a graphics subsystem or an external image
format.

Serial must remain authoritative for automated smoke tests and low-level boot
diagnostics. Kernel startup must also remain independent of framebuffer details.

## Decision

Extend the architecture-independent console contract with one optional semantic
operation for showing the system boot banner. The common console implementation
provides a compact ASCII fallback containing the Arwill mark, manifesto line,
version, and readiness state.

The x86-64 Limine framebuffer console implements that operation with the current
built-in 5x7 glyphs: a dark background, scaled Arwill mark and name, manifesto
line, version, and readiness state. It writes the ASCII fallback to its wrapped
serial console, renders the specialized framebuffer presentation, positions the
text cursor below it, and then resumes ordinary serial-output mirroring.

The presentation uses fixed compile-time colors and strings. It adds no image
asset, decoder, font loader, animation, public graphics API, or configuration.

## Consequences

Graphical boots have a deliberate first screen while serial logs retain stable
plain text. Kernel startup requests intent through the console contract and does
not depend on framebuffer primitives.

The splash is deliberately static and limited to framebuffers supported by the
existing console. Normal shell output remains the same basic text renderer and
can eventually clear the splash through its existing bottom-of-screen behavior.

## Alternatives Considered

Embedding a bitmap was rejected because it would introduce an asset pipeline
and resolution behavior for one fixed screen. Drawing framebuffer primitives
from kernel startup was rejected because it would cross the console boundary.
Replacing the serial banner with control sequences was rejected because serial
logs and smoke tests must stay plain and deterministic.

## Revisit

Revisit only when Arwill has a current requirement for a general graphics
contract, multiple display modes, runtime themes, or an asset-loading path.
