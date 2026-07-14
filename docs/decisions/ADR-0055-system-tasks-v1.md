# ADR-0055: System Tasks v1

Status: accepted

## Context

ADR-0054 introduced stackful cooperative kernel contexts, but the main shell
loop still called TCP polling and remote-console session servicing directly.
The new mechanism needs natural, long-lived system consumers without adding
kernel preemption or changing the AWP scheduler.

Remote shell commands can launch manual kernel built-ins. Therefore a system
task that owns the remote shell may initiate a nested cooperative dispatch;
one global scheduler return context would lose the outer system-task frame.

## Decision

Add explicit `system` and `kernel` kinds to the fixed kernel process table.
Manual `run` built-ins retain the `kernel` kind and are dispatched only by the
existing launch and hidden `step` paths. At shell startup, create two automatic
system tasks:

- `network-poll` performs one bounded `arwill_ipv4_poll_tcp` pass;
- `remote-console` advances connection state, authentication, one-byte input
  fairness, remote shell dispatch, timeout handling, and disconnect cleanup.

Each task yields after one pass. The main loop dispatches the `system` kind
before AWP polling and no longer calls remote-console servicing directly.

Use a scheduler context local to each dispatch invocation. Save and restore the
previous current task and scheduler target around a nested dispatch. This lets
the remote-console task execute a canonical `run` command and later yield back
to the outer main-loop dispatcher correctly.

Expose the distinction in `system`, `ps`, and `top`. Keep both system tasks in
the same eight-slot manager and shared kernel address space. Do not add a new
shell command, service registry, task priorities, sleep queues, event wakeups,
kernel preemption, SMP, or a serial-shell task.

## Consequences

Network and remote-console progress now use the same honest yield/resume model
as other stackful kernel work. Their control flow can retain ordinary local
state across future yield points, and process inspection shows their lifecycle
and run counts.

The two permanent tasks consume two process slots, leaving six slots for
manual kernel built-ins. Dispatch is still a busy cooperative service loop:
both tasks run once per pass even when no frame or input is available. A task
that fails to yield still blocks all kernel progress.

The remote shell session and environment remain owned by the non-returning
shell function; system-task contexts hold pointers to that lifetime. The serial
shell remains in the main loop.

## Verification

Run the full serial/TCP QEMU smoke test. Verify both system tasks remain ready
and visible, all existing remote authentication and service operations work,
and remote AWP output remains responsive. From the remote console, launch and
step `counter` to completion, then continue using `system`, `top`, and `exit` on
the same connection to prove nested dispatch returns to the system task.
