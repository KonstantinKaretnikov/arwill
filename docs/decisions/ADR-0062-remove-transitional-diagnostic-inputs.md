# ADR-0062: Remove Transitional Diagnostic Inputs

Status: accepted

## Context

ADR-0050 grouped owner-facing inspection under `system`, `devices`, and
`network` in Arwill 0.17.0. It temporarily retained twelve older exact-match
inputs while the grouped surface stabilized. No current test, completion,
documentation, or subsystem consumes those inputs, and retaining them leaves
historical command vocabulary in the canonical dispatcher.

## Decision

Remove `uptime`, `pciinfo`, `netinfo`, `netcfg`, `ping`, `tcpinfo`, `meminfo`,
`blkinfo`, `irqinfo`, `schedinfo`, `userinfo`, and `ownerinfo` from dispatch.
Keep the grouped inspection commands unchanged. Keep the distinct internal
smoke commands governed by ADR-0049 unchanged.

## Consequences

The shell has one command path for each inspection operation. A caller using a
retired transition input now receives the normal unknown-command response.
This does not remove a documented user command or a smoke-test contract.

## Verification

Build Arwill and run native plus QEMU checks. Verify the grouped inspection
surface and ADR-0049 smoke commands retain their behavior, and verify the
retired names do not reappear in command dispatch, help, or completion.
