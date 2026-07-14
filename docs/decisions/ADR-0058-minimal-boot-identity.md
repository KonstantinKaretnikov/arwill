# ADR-0058: Minimal Boot Identity

Status: accepted

## Context

ADR-0057 added a specialized framebuffer splash and a matching ASCII logo.
Although bounded, that presentation adds a second rendering path and visual
surface that does not fit Arwill's current minimal operating-system character.

The useful boot contract is only the system identity, version, and readiness.

## Decision

Emit exactly one identity line through the ordinary console path:

```text
Arwill <version> ready
```

Serial and framebuffer receive the same text. Remove the optional boot-banner
console operation, scaled framebuffer drawing, colors used only by the splash,
ASCII logo, tagline, and presentation-specific host test.

The shell prompt follows directly. Configuration and help remain available
through their canonical commands rather than unsolicited startup hints.

## Consequences

Boot is visually plain, deterministic, and uses only the existing text-console
contract. Serial remains authoritative and framebuffer behavior has no special
startup case.

ADR-0057 is superseded. A future richer boot presentation requires a new
explicit decision; it is not the default direction.
