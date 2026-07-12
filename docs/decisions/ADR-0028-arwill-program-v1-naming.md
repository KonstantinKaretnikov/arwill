# ADR-0028: Arwill Program v1 Naming

Status: Accepted

## Context

`API1`, `.api`, and `/programs` are easy to confuse with programming APIs and
are less direct than the simple application vocabulary Arwill wants.

## Decision

Rename the stored executable contract to Arwill Program v1:

- magic `AWP1`;
- extension `.awp`;
- applications live under `/apps`;
- the first application is `/apps/hello.awp`.

This is a clean migration. The loader does not retain `API1` compatibility or
the old `/programs/hello.api` path.

## Consequences

The owner-facing names are shorter and unambiguous. The scope remains the same:
one code page, the existing ring 3 syscall ABI, and no ELF or general toolchain.
