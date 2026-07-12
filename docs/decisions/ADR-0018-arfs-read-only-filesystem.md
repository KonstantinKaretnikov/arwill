# ADR-0018: ARFS Read-Only Filesystem

Status: accepted

## Context

Arwill now has a block-device contract and a QEMU ATA PIO path that can read
real sectors from a deterministic raw test disk image. The next roadmap step is
to make shell filesystem commands use storage-backed data instead of the static
boot catalog.

Implementing a production filesystem format, writable metadata, a block cache,
partitions, file handles, or streaming reads would make this milestone too
large. The immediate need is a small, testable read-only filesystem path.

## Decision

Add ARFS v1, a tiny read-only filesystem format for the QEMU raw test disk.
ARFS uses:

- a superblock at a fixed sector;
- a text manifest describing directories and files;
- fixed data sectors for small file contents.

Mount ARFS during the x86-64/QEMU boot path after ATA PIO block-device
initialization. If ARFS cannot mount, fall back to the static boot catalog.

Use ARFS as the primary filesystem in the normal QEMU test path. The existing
shell commands `ls`, `cd`, `cat`, `stat`, and path completion continue to use
the filesystem contract, but their data now comes from the disk image.

## Consequences

Arwill now has a real storage-backed read-only filesystem path. The static boot
catalog remains useful as a fallback and as a simple contract implementation,
but it is no longer the primary happy path in QEMU smoke tests.

ARFS is intentionally not a general-purpose filesystem. It has no writes, no
allocation, no timestamps, no permissions, no open handles, no streaming reads,
no directories beyond manifest entries, and no crash-consistency model.

## Alternatives Considered

ISO9660 was considered because Arwill already boots from an ISO image, but it
would require a larger parser before the project has a block cache or richer
test fixtures.

A tar or initrd format was considered, but ARFS's explicit directory entries
make it easier to test `ls`, `cd`, `cat`, and `stat` without adding archive
semantics.

Keeping the static catalog as the primary filesystem was rejected because it
would not satisfy the storage-backed filesystem milestone.

## Revisit

Revisit when Arwill needs a standard read-only format, larger files, a block
cache, partitions, streaming reads, file handles, writes, or persistence
semantics beyond this deterministic test image.
