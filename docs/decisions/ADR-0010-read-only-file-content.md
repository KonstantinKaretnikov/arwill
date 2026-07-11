# ADR-0010: Read-Only File Content

Status: accepted

## Context

The shell can navigate and list a static boot catalog, but files were only
names. The next useful interactive step is a command that can inspect small
text files without pretending Arwill already has a storage driver or a real
filesystem implementation.

## Decision

Add a filesystem `read_file` contract for whole-file, read-only access by path.
The first implementation is still the static boot catalog. It exposes text
payloads for `/system/identity` and `/boot/limine/limine.conf`, and marks boot
artifacts such as `/boot/kernel.elf` as binary.

Add a shell `cat [path]` command to display text files. `cat` resolves absolute
and relative paths through the same current-directory logic as `cd` and `ls`.
It reports directories, missing files, and binary files explicitly.

Add a shell `stat [path]` command to display directory entry counts and file
metadata exposed by the same read-only catalog.

## Consequences

The shell can now inspect a tiny subset of file contents while preserving the
honest boundary that Arwill does not yet read from storage. The filesystem
contract grows by one narrow operation, but still has no open handles, streaming
reads, writes, allocation, permissions, or mounts.

Tab completion works for `cat` and `stat` because they use the same path
completion table as `ls`.

## Alternatives Considered

Embedding `cat` directly in the shell with hard-coded paths was rejected because
it would bypass the filesystem boundary. Adding a full VFS or block storage
stack was rejected as too much architecture before the current requirement.

## Revisit

Revisit when Arwill adds storage-backed files, file descriptors, binary-safe
streaming reads, or a distinction between mounted filesystems.
