# ADR-0029: ARFS v2 Mutable Core

Status: accepted

## Context

ARFS v1 can overwrite only `/owner/note`. Arwill now needs a small foundation
for creating directories and arbitrary text or binary files without adopting a
general-purpose filesystem.

## Decision

Introduce ARFS v2 with:

- the existing two-sector text manifest as mutable metadata;
- a fixed table of at most 16 entries;
- paths shorter than 64 bytes and names shorter than 32 bytes;
- contiguous allocation after a superblock-defined first data sector;
- files smaller than 2048 bytes;
- create-directory, whole-file byte write, and remove operations;
- free space inferred from file ranges in the manifest.

Writes persist file data before replacing the manifest. Directories must be
empty before removal. The existing shell `write` contract remains restricted
to `/owner/note` until separate shell commands expose the new operations.

## Consequences

The filesystem core can persist arbitrary text and binary files and directories
without a bitmap or block cache. Deleted and replaced ranges become reusable
when they disappear from the manifest.

There is no journal, atomic metadata replacement, recovery, fragmentation
handling, append, rename, timestamps, permissions, or compatibility with ARFS
v1. A failed metadata write can leave unreferenced data, but not a manifest
that points to data which was never written.

## Alternatives Considered

A bitmap was deferred because the small fixed entry table can describe all
occupied ranges. A binary directory format and journaling were deferred until
the limits of the text manifest become a current problem.

## Revisit

Revisit when files need to exceed four sectors, entry count or fragmentation
becomes restrictive, or power-loss recovery becomes a requirement.
