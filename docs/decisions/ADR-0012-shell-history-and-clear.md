# ADR-0012: Shell History and Clear

Status: accepted

## Context

The serial shell is now used often enough that retyping commands slows down
development. Arwill still has no terminal abstraction, cursor model, or full
line editor.

## Decision

Keep shell usability features local to `kernel/shell.c`:

- `clear` emits ANSI clear-screen and cursor-home escape sequences.
- Up and Down browse a small in-memory command history.
- The escape parser recognizes only the arrow sequences needed for history.

The history is not persistent and is not shared with any other subsystem.

## Consequences

Interactive QEMU sessions become faster without introducing a terminal driver
or readline-style editor. The shell remains byte-oriented and intentionally
plain.

The current history model is simple: recalling history replaces the whole input
line, and editing a recalled line makes it the active line.

## Alternatives Considered

A full line editor with cursor movement and editing in the middle of a line was
rejected because it would add terminal assumptions before Arwill needs them.
Persisting history was rejected because there is no writable filesystem.

## Revisit

Revisit when Arwill adds a real terminal layer, command-line cursor movement,
quoted arguments, persistent storage, or a richer shell process model.
