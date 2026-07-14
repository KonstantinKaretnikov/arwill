# ADR-0023: Cooperative Yield for Kernel Processes

Status: superseded by ADR-0054

## Context

Arwill needs a simple process model for board and OS experiments. The previous
kernel process model was cooperative but run-to-completion: a process entry ran
once and immediately became finished.

The project does not currently need preemptive scheduling, saved CPU register
contexts, independent kernel stacks, or a full task switcher. Those would add a
large amount of machinery before the simple process model has earned it.

## Decision

Process entries now return an explicit process result:

- `finished` with an exit code;
- `yielded`, which places the process back in the ready state.

The shell exposes `step`, which runs one cooperative pass over ready processes.
The first saved progress mechanism is explicit process state represented by the
process run count. The built-in `counter` process uses this to print one step,
yield, and continue on later `step` commands.

This is not a saved hardware execution context. It is deliberate cooperative
progress for simple built-in kernel processes.

## Consequences

Arwill can now demonstrate a process that pauses and continues without hiding a
large scheduler behind the shell. `ps` can show a yielded process as `ready`,
and later as `finished`.

Future work can add real saved CPU contexts, independent stacks, or preemption,
but those should be introduced by a separate ADR and testable milestone.

ADR-0054 later replaces returned `yielded` results with stackful cooperative
contexts and a yield call that resumes at the following instruction.
