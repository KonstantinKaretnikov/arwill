# ADR-0013: Shell Input Layout Normalization

Status: accepted

## Context

Arwill's serial shell accepts byte input from the host terminal. When the host
keyboard is left in a Russian layout, typing command key positions sends UTF-8
Cyrillic characters instead of ASCII command letters. Arwill does not need
Cyrillic text entry yet, but switching the host layout before every shell
session slows down development.

## Decision

Normalize standard Russian-layout UTF-8 input in `kernel/shell.c` before it is
stored in the shell line buffer.

The normalizer maps Cyrillic codepoints back to their ASCII key positions, so
typing command keys in the Russian layout still produces ASCII commands and
paths internally. The shell echoes the normalized ASCII character, stores ASCII
history entries, and keeps command parsing, completion, and path resolution
ASCII-only.

The normalizer also treats a literal `.` byte as `/` after Russian-layout input
has been seen on the current line. This covers the common Russian-layout slash
key behavior while preserving normal English-layout `.` input before any
Russian-layout character appears.

## Consequences

Developers can use the serial shell without constantly switching the host
keyboard layout to English. This is not Unicode text support: file names,
commands, paths, and history remain ASCII-only.

The behavior is intentionally shell-local. The input contract still exposes raw
bytes, and no keyboard driver or locale subsystem is introduced.

## Alternatives Considered

Adding true Cyrillic text support was rejected because the shell and filesystem
are currently ASCII-only. Moving layout handling into the input contract was
rejected because the current input source is serial bytes, not keyboard scan
codes. Requiring users to switch layouts manually was rejected as needless
friction for routine QEMU sessions.

## Revisit

Revisit when Arwill adds a real keyboard driver, terminal layer, Unicode string
handling, non-ASCII filenames, or configurable input locales.
