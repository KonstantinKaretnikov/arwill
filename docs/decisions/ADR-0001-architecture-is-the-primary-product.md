# ADR-0001: Architecture Is the Primary Product

Status: accepted

## Context

Arwill is not trying to maximize generated code volume. It exists to explore
an engineering model where human intent and AI-generated implementation are
connected through explicit architecture and documented decisions.

## Decision

Treat the architecture as the primary product. Source code is one
implementation of that architecture and must remain explainable, testable, and
replaceable.

## Consequences

Architectural documents and ADRs are first-class project artifacts. Code that
works but hides coupling is not good enough. Small working increments are
preferred over speculative subsystem growth.

## Alternatives Considered

Generating a larger hobby kernel first was rejected because it would obscure
the project experiment. Treating documentation as secondary was rejected
because future AI agents need durable context.

## Revisit

Revisit if the repository process becomes too heavy for small, reversible
changes. The principle should remain, but the amount of ceremony may change.
