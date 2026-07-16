# ADR-0009: Shell Tab Completion

Status: accepted; cursor-movement limit superseded by ADR-0063

## Context

The serial shell now has enough commands and paths for interactive typing to
benefit from completion. Arwill still has no terminal driver, cursor addressing,
or line-editing library.

## Decision

Implement Tab completion inside the shell. Command completion matches built-in
command names. Path completion is available for path-oriented commands such as
`cd`, `ls`, `cat`, and `stat` through the existing read-only filesystem listing
contract.

The shell completes a unique match inline. If multiple matches exist and no
longer common prefix can be inserted, it prints candidates and redraws the
prompt plus the current input line.

For `exec [image] [file]`, both positions use filesystem path completion. The
first selects the AWP image and the second selects its one optional launch file.
No completion is offered after the second path.

## Consequences

Completion improves the interactive serial workflow without introducing a
terminal abstraction or a broad line editor. Completion remains shell-local
state and depends only on the existing console, input, and filesystem
contracts.

The original UI intentionally omitted cursor movement. ADR-0063 adds bounded
single-line cursor editing while retaining the other exclusions: no reverse
search, quoted argument parsing, or command-specific completion hooks.

## Alternatives Considered

A full readline-style editor was rejected because it would add too much surface
area before the shell needs it. Filesystem-specific completion hooks were
rejected because the current filesystem contract can already list entries.

## Revisit

Revisit when Arwill adds a richer terminal, quoted arguments, command-specific
completion hooks, or a writable filesystem.
