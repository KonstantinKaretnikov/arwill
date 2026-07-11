# ADR-0009: Shell Tab Completion

Status: accepted

## Context

The serial shell now has enough commands and paths for interactive typing to
benefit from completion. Arwill still has no terminal driver, cursor addressing,
history, or line-editing library.

## Decision

Implement Tab completion inside the shell. Command completion matches built-in
command names. Path completion is available for path-oriented commands such as
`cd`, `ls`, `dir`, and `cat` through the existing read-only filesystem listing
contract.

The shell completes a unique match inline. If multiple matches exist and no
longer common prefix can be inserted, it prints candidates and redraws the
prompt plus the current input line.

## Consequences

Completion improves the interactive serial workflow without introducing a
terminal abstraction or a broad line editor. Completion remains shell-local
state and depends only on the existing console, input, and filesystem
contracts.

The UI is intentionally plain: no cursor movement, reverse search, history, or
quoted argument parsing.

## Alternatives Considered

A full readline-style editor was rejected because it would add too much surface
area before the shell needs it. Filesystem-specific completion hooks were
rejected because the current filesystem contract can already list entries.

## Revisit

Revisit when Arwill adds a richer terminal, command history, quoted arguments,
or a writable filesystem.
