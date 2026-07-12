# ADR-0030: ARFS v2 Shell Mutations

Status: accepted

## Context

ADR-0029 added bounded directory creation, whole-file byte writes, removal,
and contiguous-space reuse behind the filesystem contract. The shell still
exposes only the earlier `/owner/note` text overwrite behavior.

## Decision

Expose the ARFS v2 mutation contract through four canonical shell commands:

- `mkdir [path]` creates one directory whose parent already exists;
- `write [path] [text]` creates or replaces a complete text file;
- `writehex [path] [hex]` creates or replaces a complete binary file from an
  even number of hexadecimal digits;
- `rm [path]` removes a file or an empty directory.

Paths use the shell's existing absolute and current-directory resolution. The
commands do not add aliases, recursive removal, append, rename, quoting, or a
general binary transfer protocol.

## Consequences

The owner can exercise all current ARFS v2 mutations from the serial shell.
Input remains bounded by the shell line size, even though the filesystem core
can accept larger whole-file writes from other callers. Existing ARFS v2 entry,
path, file-size, allocation, and crash-consistency limits remain unchanged.

## Verification

The bounded QEMU smoke path creates text and binary files, reboots with the same
disk, verifies persistence, runs the existing `/apps/hello.awp`, removes the
created entries, and checks reuse of the released first data sector.
