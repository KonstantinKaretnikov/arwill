# ADR-0044: AWP File I/O and Editor

Status: accepted

## Context

The first interactive AWP can only read terminal bytes, write terminal bytes,
and read the monotonic clock. A useful editor needs bounded file access without
introducing file descriptors, open handles, seek, append, or POSIX semantics.

## Decision

Add two AWP syscalls: whole-text-file read and whole-text-file write. Both copy
paths and bytes across the ring 3 boundary with explicit bounds and use the
existing filesystem contract. They do not expose block devices or ARFS internals.

Add `/apps/edit.awp`, a single-file ASCII ANSI-terminal editor. It prompts for
a path and supports arrows, Enter, Backspace, Delete, Home/End, Ctrl+S, Ctrl+Q,
and Ctrl+C. It has a 2048-byte document limit, one viewport, a status line, and
no undo, selection, clipboard, search, syntax highlighting, mouse, or Unicode.

Increase the bounded ARFS entry table to 24 and file buffer to 8192 bytes only
because the current config and editor images need storage beside the existing
smoke fixture. Keep edited text capped at 2048 bytes. AWP1 stays a flat stored
image with at most two 4096-byte code pages; this is not a new executable format.

## Consequences

AWP programs gain useful persistence while the filesystem interface remains
whole-file and bounded. Concurrent writes to the same path use last completed
write wins; locking and merge behavior are out of scope.

## Verification

Edit and save a text file, read it through `cat`, reboot and read it again, then
run two editor/calculator instances in separate sessions.
