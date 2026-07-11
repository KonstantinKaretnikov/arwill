# ADR-0016: Kernel Cooperative Processes

Status: accepted

## Context

Arwill needs to start running work units from inside the OS. A full user-space
process model requires several missing subsystems: address spaces, privilege
transitions, an ELF program loader, syscalls, timer interrupts, context
switching, and a scheduler.

Adding shell commands that pretend to launch normal user-space programs would
make the architecture misleading.

## Decision

Add a kernel process manager with a fixed-size process table. Each process has
a PID, name, state, run count, exit code, entry function, and opaque context.

The first execution model is cooperative and run-to-completion. The shell adds:

- `run [name]` to spawn a built-in kernel process and run ready processes;
- `ps` to display the process table.

The first built-in kernel processes are `hello` and `counter`.

## Consequences

Arwill can now launch named work units from inside the OS and inspect their
lifecycle state. This creates a real process contract without inventing
user-space features prematurely.

The model is intentionally limited. A process runs synchronously in kernel
space, shares the kernel address space, cannot block independently of the shell,
and is not preempted.

## Alternatives Considered

Running function pointers directly from the shell was rejected because it would
hide process lifecycle state inside command parsing.

Calling these user programs was rejected because there is no program loader,
user/kernel boundary, syscall ABI, or address-space isolation yet.

Implementing a preemptive scheduler now was rejected because Arwill does not
yet have the interrupt and context-switching foundation needed to make it
honest and testable.

## Revisit

Revisit this decision when Arwill adds timer interrupts, saved CPU contexts,
virtual memory address spaces, user-mode entry, syscall handling, or an ELF
loader.
