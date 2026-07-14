# ADR-0053: Retire the Kernel Hello Built-in

Status: accepted

## Context

`run hello` was introduced with the first cooperative process table to prove
that a named kernel work unit could be spawned and observed. The process model
now has stronger consumers: `counter` demonstrates cooperative yield and
resume-by-state, while `userhello`, `userbad`, and stored AWP programs exercise
the ring 3 runtime.

The run-to-completion kernel hello callback no longer validates a distinct
behavior. Keeping it also makes the process list look more like an intended
system service than the historical bring-up probe it is.

## Decision

Remove the `hello` kernel built-in in Arwill `0.17.3`. Remove its callback and
its `run` completion entry. `run hello` is no longer accepted and reports an
unknown process.

Keep `counter` as the only cooperative kernel built-in. Keep `userhello` as a
narrow generated ring 3 syscall diagnostic and `/apps/hello.awp` as a stored
AWP loader fixture; those names refer to different mechanisms and are outside
this decision.

This decision supersedes ADR-0016 only for its current built-in set. The fixed
cooperative process table and `run`/`ps` contracts remain unchanged.

## Consequences

The kernel command surface loses a redundant bring-up demonstration without
changing the process-manager contract. Kernel process PIDs in smoke output
shift down by one. AWP PIDs and behavior remain unchanged.

## Verification

Build Arwill and run native tests plus the QEMU serial/TCP smoke path. Verify
that `run hello` is rejected, that the available process list is exactly
`counter userhello userbad`, and that `counter`, both generated ring 3 probes,
stored AWP execution, `ps`, and `top` continue to work.
