# ADR-0031: User Read Syscall and Minimal Calculator

Status: accepted

## Context

Stored AWP images could write output and exit, but had no way to consume the
serial input already used by the shell. That prevented a small interactive
application from being useful.

## Decision

Add syscall `3` (`read`) to the first AWP syscall ABI. It reads a bounded number
of bytes from the platform input into a user stack range and blocks on the
existing serial input device. No file descriptors, buffering layer, or
preemptive scheduling are introduced.

Add `/apps/calc.awp`, built from `apps/calc/`, as the first interactive stored
application. It accepts one expression in the form `integer operator integer`,
where the operator is `+`, `-`, `*`, or `/`, then prints the integer result and
exits.

## Consequences

The ring 3 boundary now includes a deliberately narrow input path. A malformed
expression prints `error`; division by zero is rejected. The calculator is a
demonstration, not a shell replacement or general language runtime.

## Verification

The QEMU smoke test runs `/apps/calc.awp`, enters `12*7`, and observes `84` and
`exec: exited 0` before continuing with the filesystem checks.
