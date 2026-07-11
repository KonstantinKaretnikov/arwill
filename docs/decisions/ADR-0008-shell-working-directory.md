# ADR-0008: Shell Working Directory

Status: accepted

## Context

After adding `ls`, the next requested command is `cd`. A change-directory
command needs a current working directory, but Arwill still has no process
model, userspace, scheduler, or per-process file descriptor table.

## Decision

Store the current working directory as local state inside the serial shell.
The kernel does not expose a global working directory. The shell resolves
absolute paths, relative paths, `.`, and `..`, then validates the target through
the read-only filesystem listing contract.

Add `pwd` so the current directory can be observed.

## Consequences

`cd` is useful for the current interactive shell without inventing a process
model. `ls` and `dir` without arguments list the current directory. The prompt
shows the current directory as `Arwill:/path>`.

This is intentionally shell state, not system-wide kernel state.

## Alternatives Considered

A global kernel current directory was rejected because it would create hidden
state before Arwill has processes. Adding `cd` as a no-op was rejected because
it would mislead users. Implementing Unix-like process current directories was
rejected as too large for this milestone.

## Revisit

Revisit when Arwill has tasks or processes. At that point, current directory
state should move into the process or task model rather than remaining only in
the shell.
