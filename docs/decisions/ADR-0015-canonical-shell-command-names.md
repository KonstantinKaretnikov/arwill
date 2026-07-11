# ADR-0015: Canonical Shell Command Names

Status: accepted

## Context

Early shell milestones accepted a few aliases: `dir` for `ls`, `info` for
`stat`, and `poweroff` for `exit`. Aliases make the command table, help output,
completion behavior, and documentation broader than the actual system behavior
needs to be.

## Decision

Keep one canonical shell command name for each operation:

- `ls` lists directories;
- `stat` displays metadata;
- `exit` powers off the current QEMU session.

Remove the alias commands `dir`, `info`, and `poweroff` from command parsing,
help output, and completion.

## Consequences

The shell command surface is smaller and easier to document. Users must use the
canonical command names. Historical ADRs that mention aliases are superseded by
this decision for current behavior.

## Alternatives Considered

Keeping aliases was rejected because Arwill's shell is still small and benefits
more from a precise command vocabulary than from convenience synonyms.

## Revisit

Revisit only if Arwill gains a user-configurable shell layer where aliases are
data rather than built-in commands.
