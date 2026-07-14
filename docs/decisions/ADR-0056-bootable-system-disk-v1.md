# ADR-0056: Bootable System Disk v1

Status: accepted

## Context

Arwill boots from a Limine hybrid ISO but stores ARFS on a separately attached
raw test disk. That split is useful for early storage tests but is not a usable
distribution model: QEMU and UTM must be configured with two media devices,
and the operating system appears to have no applications when the second image
is omitted.

Arwill needs one bootable and persistent image without first building an
interactive installer, a partition parser, or an in-kernel formatter.

## Decision

Produce `build/arwill.img` as the normal system-disk artifact. Its first 16 MiB
contain the existing BIOS/UEFI hybrid Limine image. A fixed 1 MiB ARFS region
begins at LBA 32768 and extends for 2048 512-byte sectors. The complete image is
therefore 17 MiB.

Add an architecture-independent bounded block-device region contract. It
translates region-relative reads and writes into a parent device while rejecting
requests outside its declared sector count. The x86-64/QEMU boot wiring exposes
only the fixed ARFS region to the filesystem and shell storage diagnostics.
ARFS keeps its existing relative on-disk layout and does not learn about Limine,
partitions, or physical disk offsets.

`make build` and `make image` produce the combined image. `make run` boots it as
the only IDE disk, with no CD-ROM. The QEMU smoke test copies it before making
persistent mutations, then reboots from that same copy.

The hybrid ISO remains a build intermediate and optional compatibility artifact.
The small ARFS seed remains an internal deterministic image-generation input;
neither must be attached by the user.

## Consequences

Arwill can be distributed and booted as one disk image, and filesystem changes
persist in that image. Bounded region accounting prevents ARFS allocation from
overwriting boot files even though ARFS has no partition awareness.

The layout is fixed at build time. It is not GPT/MBR partition discovery, a
general volume manager, an installer, disk cloning, upgrades, or resize support.
Rebuilding `arwill.img` intentionally creates a newly seeded system disk; normal
repeated `make run` invocations reuse it until a dependency changes.

Automated verification covers QEMU BIOS boot. UTM remains a manual target using
x86_64 legacy BIOS, the image as IDE primary disk, and its built-in serial
terminal.

UTM may assign its only imported IDE image to a legacy slot other than primary
master. The ATA PIO driver therefore probes master and slave positions on both
primary and secondary channels. A focused QEMU smoke boots the system image as
secondary master and verifies both disk publication and ARFS mount.

## Alternatives Considered

An interactive installer was deferred because it would require safe disk
selection, formatting, bootloader installation, and failure recovery before
Arwill has a general storage-management surface.

Putting mutable files in the ISO9660 filesystem was rejected because the boot
filesystem is read-only and ARFS already owns persistence semantics.

Teaching ARFS a physical base LBA was rejected because that would couple a
filesystem implementation to the platform image layout. A bounded block-device
view is a current, narrow use case and preserves the existing storage contract.

## Revisit

Revisit when Arwill needs installation onto a blank physical disk, multiple
storage devices, discovered partitions, upgrades that preserve an existing
ARFS region, or UEFI-only disk boot verification.
