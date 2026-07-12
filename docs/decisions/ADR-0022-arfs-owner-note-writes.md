# ADR-0022: ARFS Owner Note Writes

Status: accepted

## Context

Arwill has storage-backed ARFS reads and a single-owner product model. The next
roadmap step is persistent writable storage, but a general filesystem allocator,
directory mutation model, free-space tracking, file handles, append behavior,
and crash consistency would make the first write milestone too large.

The project needs a small, honest write path that reaches the QEMU disk image
and can be verified across reboot.

## Decision

Add block-device writes to the storage contract and implement ATA PIO sector
writes for the QEMU target.

Extend ARFS with one reserved writable text file:

- path: `/owner/note`;
- data sector: reserved in the deterministic test disk image;
- state sector: stores the current note size;
- shell command: `write /owner/note [text]`.

On write, ARFS overwrites the reserved data sector, persists the new size in the
reserved state sector, and updates the in-memory file entry. On mount, ARFS
reads the state sector and restores the note size before serving reads.

## Consequences

Arwill now has a real persistent write path: a shell command changes data on
the QEMU raw disk image, and the bounded smoke test verifies that the change is
visible after a second QEMU boot using the same disk image.

The design is intentionally narrow. ARFS still has no general allocation,
arbitrary file creation, append, delete, rename, directory mutation, block
cache, journal, or crash-consistency model. The write path is suitable for
proving persistence and for owner notes, not for general-purpose storage yet.

## Alternatives Considered

Adding a general mutable ARFS manifest was deferred because it would require
allocation and update rules before the project has a block cache or recovery
story.

Implementing append first was deferred because overwrite of one reserved file
is easier to reason about and verify.

Keeping writes as an in-memory shell feature was rejected because it would not
satisfy the persistent writable filesystem milestone.

## Revisit

Revisit when Arwill needs arbitrary file creation, directory mutation, append,
delete, rename, free-space tracking, block caching, crash consistency, or a
standard filesystem format.
