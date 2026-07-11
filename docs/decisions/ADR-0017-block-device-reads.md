# ADR-0017: Block Device Reads

Status: accepted

## Context

Arwill's shell filesystem commands still use a static boot catalog compiled
into the kernel. The next roadmap step is to prove real storage access without
also implementing a filesystem parser, block cache, write path, or partition
handling.

The existing QEMU platform already uses x86-64 port I/O for serial and power
operations, so a small ATA PIO read path is a reasonable first storage
milestone.

## Decision

Add a read-only kernel block-device contract for bounded sector reads by LBA.

Add a QEMU ATA PIO implementation for the first block device. The host run and
smoke paths attach a deterministic raw test disk image. The shell adds
`blkinfo`, which reports the detected block device and reads LBA 1 from the
test image.

Use QEMU machine type `pc` for the current run and smoke paths because it
exposes the legacy IDE ports used by the first ATA PIO implementation. The
previous `q35` machine type remains a possible future target, but it did not
expose this legacy ATA path in the same simple way.

## Consequences

Arwill now has real sector reads from a QEMU-attached disk image. This is still
below the filesystem layer: `ls`, `cd`, `cat`, `stat`, and path completion keep
using the static boot catalog until a storage-backed filesystem is implemented.

The block-device contract is intentionally narrow. It has no writes, cache,
partition table parsing, DMA, interrupt-driven I/O, AHCI, or virtio-blk support.

## Alternatives Considered

Implementing ISO9660 or another filesystem immediately was rejected because it
would combine the block-device and filesystem milestones.

Implementing AHCI or virtio-blk first was deferred because the ATA PIO path is
smaller and fits the current port-I/O foundation.

Pretending the static catalog was disk-backed was rejected because it would
misrepresent the architecture.

## Revisit

Revisit when Arwill needs block writes, a block cache, partitions, AHCI,
virtio-blk, interrupt-driven disk I/O, or support for QEMU machine types that
do not expose legacy ATA ports.
