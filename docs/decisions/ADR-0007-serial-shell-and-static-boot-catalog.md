# ADR-0007: Serial Shell and Static Boot Catalog

Status: accepted

## Context

The next requested direction is keyboard input, a shell, directories, filesystem
listing, and eventually a storage driver. Implementing a real disk storage
driver and filesystem in the same step would create too many unreviewed
boundaries at once.

## Decision

Add the first interactive milestone as a serial shell over the existing QEMU
COM1 path. Terminal keyboard input reaches the kernel as serial bytes. Add a
small input contract, a read-only filesystem listing contract, and a static boot
catalog used by `ls`.

The `ls` command is the primary command name. `dir` was originally accepted as
an alias, but alias commands were later removed by ADR-0015.

This milestone does not implement a disk storage driver. The static boot
catalog is explicitly not a disk filesystem and does not read the ISO at
runtime.

## Consequences

Arwill now has an interactive loop that can be tested through QEMU serial I/O.
The user can type `help`, `version`, `ls`, and `halt`.

The filesystem contract is intentionally narrow: list entries for a path. There
is no file open, file read, write, allocation, mount table, block cache, or
device discovery yet.

## Alternatives Considered

Polling a PS/2 keyboard was rejected for this step because the current QEMU run
path uses `-display none` and serial terminal I/O. A fake disk driver was
rejected because it would misrepresent the system. Implementing ISO9660 and a
block storage driver now was rejected as too large for the next small,
reviewable milestone.

## Revisit

Revisit when Arwill is ready to add a real storage block contract. At that
point, the static catalog can be replaced by a filesystem implementation backed
by a storage driver without changing the shell command contract.
