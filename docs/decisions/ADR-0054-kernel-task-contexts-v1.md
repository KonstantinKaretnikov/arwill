# ADR-0054: Kernel Task Contexts v1

Status: accepted

## Context

ADR-0023 modeled cooperative progress by returning `yielded` from a process
entry and reconstructing that entry's progress from its run count. This proved
the lifecycle and shell interfaces, but it was not a suspended execution
context: local variables, nested calls, and the instruction after the yield
did not survive.

Arwill now needs honest cooperative suspension for kernel system work before
moving long-lived polling loops into tasks. Kernel preemption and SMP remain
separate, substantially larger milestones.

## Decision

Give each of the eight fixed kernel-task slots a preallocated, 16-byte-aligned
8 KiB stack and one saved architecture context. Inject a narrow context backend
into the architecture-independent process manager at boot.

On x86-64, switch contexts only at a normal C call boundary. Save and restore
the stack pointer plus the System V ABI callee-saved registers (`RBP`, `RBX`,
and `R12` through `R15`). A fresh stack starts in an architecture bootstrap,
then enters the architecture-independent process trampoline.

`arwill_process_yield(runtime)` marks the current task ready, switches back to
the scheduler context, and later returns at the next instruction when that task
is dispatched again. Returning from the task entry finishes it with its exit
code. Reusing a finished slot initializes a fresh context on that slot's stack.

Keep the fixed table, shared kernel address space, cooperative dispatch passes,
and existing `run`, `ps`, and hidden `step` interfaces. Use `counter` to prove
that an automatic stack-local value survives two yields.

Do not move network polling, remote-console polling, or the serial shell into
tasks in this milestone. Do not add timer-driven kernel preemption, SMP,
dynamic stacks, stack guard pages, priorities, blocking primitives, or a new
public shell command.

## Consequences

Kernel-task code can now use ordinary loops, local variables, and nested calls
across explicit yield points. A task that does not yield can still monopolize
kernel execution, and every task can access all kernel memory.

The manager reserves 64 KiB for its eight stacks. Stack overflow is not
contained because v1 has no guard pages. Interrupt handlers may run while a
kernel task is active, but timer interrupts do not switch kernel tasks.

AWP tasks remain a distinct process kind. Their full ring 3 interrupt frames,
per-slot address spaces, PIT preemption, and fault containment are not reused
for cooperative kernel-task switching.

## Verification

The QEMU smoke test starts `counter`, observes it ready after the first yield,
then dispatches it twice more. Output values 10, 11, and 13 come from one local
variable on the task stack, and `ps` reports three runs and a successful exit.
