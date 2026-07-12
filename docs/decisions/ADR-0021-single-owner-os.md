# ADR-0021: Single-Owner OS

Status: accepted

## Context

Arwill now has a first ring 3 user-mode path. The term "user" can easily be
misread as Unix-style user accounts, roles, permissions, login sessions, and
multi-user isolation.

The intended product direction is different: Arwill is an operating system for
one owner. The owner should have full control of the machine and should not
fight a permission model designed for mutually suspicious human users.

At the same time, letting every ordinary program run with kernel privilege
would make mistakes too destructive and would make debugging much harder.

## Decision

Arwill is a single-owner OS.

The system will not add login accounts, groups, role databases, user IDs, or
multi-user permission checks by default. The owner is the authority for the
whole machine.

Keep the kernel/user CPU privilege boundary. Ring 3 is an engineering guardrail
for ordinary programs: they use syscalls, and privileged access is added
deliberately as kernel or driver work.

Expose this model in startup output and through the canonical shell command
`ownerinfo`.

## Consequences

Arwill can stay personal and direct without growing a Unix-like permission
model too early or by accident.

The word "user" in code and docs should mean CPU user mode unless explicitly
talking about the human owner. Future work should prefer "owner" for the human
operator and "ring 3" or "user mode" for CPU privilege.

Ordinary programs do not get direct access to all memory and hardware by
default. That protects the owner from accidental damage while preserving a path
for explicit privileged kernel and driver features.

## Alternatives Considered

Adding multi-user accounts now was rejected because it does not match the
project's intended personal ownership model.

Running every program in ring 0 was rejected because it would make ordinary
program bugs indistinguishable from kernel bugs and would weaken system
integrity without giving the owner meaningful extra control.

## Revisit

Revisit only if Arwill intentionally becomes a shared machine, needs remote
untrusted users, or needs a capability model that is explicitly about limiting
different principals rather than protecting the owner from accidental program
damage.
