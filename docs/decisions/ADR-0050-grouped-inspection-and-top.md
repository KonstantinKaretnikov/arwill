# ADR-0050: Grouped Inspection Commands and Live Top

Status: accepted

## Context

Arwill accumulated a separate top-level shell command for each early
inspection milestone. The individual names made subsystem bring-up easy to
smoke-test, but they left the owner-facing command list organized by project
history rather than by current system boundaries.

The fixed process tables, memory counters, scheduler statistics, device
registry, and TCP service state are now sufficient for a small live dashboard.
AWP does not expose general system-observation syscalls, and adding them only
to implement a dashboard application would widen the user ABI without another
consumer.

## Decision

Release the grouped interface as Arwill `0.17.0`:

- `system [memory|interrupts|scheduler|runtime|owner]`;
- `devices [pci|disk0|net0]`;
- `network [ping|tcp]`;
- `top`.

Without an argument, `system` prints a bounded system summary, `devices` lists
the fixed registry, and `network` prints device, fixed IPv4, TCP, and remote
console state. Fixed subsystem arguments participate in Tab completion.

Keep `ps` as the stable one-shot process listing. Implement `top` as
nonblocking per-session shell state, not an AWP program. It clears and redraws
once per second from the existing architecture-independent contracts while the
main shell loop continues TCP service and AWP dispatch. It displays cooperative
kernel built-ins and scheduled AWP tasks in one table with an explicit `KIND`
column. `q` and Ctrl+C leave the dashboard. Do not add sorting, filtering,
colors, task control, or a new observation syscall in this milestone.

Remove the replaced top-level names from `help` and Tab completion. Retain
their exact dispatch temporarily as hidden diagnostic transition inputs:
`uptime`, `pciinfo`, `netinfo`, `netcfg`, `ping`, `tcpinfo`, `meminfo`,
`blkinfo`, `irqinfo`, `schedinfo`, `userinfo`, and `ownerinfo`. They are not
canonical aliases or a user contract and should gain no new consumers.

This decision changes the `0.16.0` command-surface rule recorded in project
guidance and supersedes the user-visible exposure portions of the earlier
subsystem ADRs. ADR-0015 still prohibits multiple public aliases for one
operation. ADR-0049 continues to govern the original internal smoke commands.

## Consequences

The owner-facing shell is smaller while detailed subsystem state remains
available. The grouping stays in the shell orchestration layer; device,
network, scheduler, memory, and user-runtime implementations do not depend on
one another.

`top` is an interactive ANSI dashboard and therefore not a stable machine
output format. `ps` remains available for a bounded snapshot. A dashboard in a
remote session uses the existing bounded TCP output queue and one-second
refresh, so it does not wait for peer acknowledgement in an AWP syscall path.

## Verification

Build Arwill and run native tests plus the QEMU serial/TCP smoke path. Exercise
each grouped command and fixed-argument completion. Verify that `help` omits
the retired names, `top` renders at least twice with distinct `KIND` values and
returns to the shell with `q`, and the existing hidden smoke paths still work.
