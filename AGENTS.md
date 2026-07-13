# Agent Guidance

This file is durable guidance for Codex sessions and other coding agents
working on Arwill.

Before changing architecture, read `MANIFESTO.md` and the relevant ADRs in
`docs/decisions/`.

Before starting a large new subsystem, check `docs/roadmap.md` and prefer the
agreed milestone order unless the project owner explicitly changes direction.

Rules for future work:

- Do not silently change public contracts.
- Do not create cross-layer dependencies to save time.
- Do not introduce abstractions without a current use case.
- Do not add a dependency without documenting why.
- Do not copy code without license and attribution.
- Prefer small, reviewable changes.
- Keep the system bootable after each completed task.
- Run all available checks before declaring completion.
- Prefer sequential milestone work with tests added alongside each new behavior.
- Update documentation when behavior or architecture changes.
- Record substantial architectural decisions as ADRs.
- Explain uncertainty rather than inventing hardware facts.
- Never claim a test passed unless it was actually executed.
- Preserve separation between architecture-independent, architecture-specific,
  and platform-specific code.
- Keep all generated code understandable by a human reviewer.
- Treat shell Russian-layout input normalization as ASCII command-entry
  convenience, not as Cyrillic text support.
- Keep shell commands canonical; do not add built-in alias commands unless a
  later ADR explicitly reverses ADR-0015.
- Distinguish process kinds precisely. `hello` and `counter` are cooperative
  kernel-managed built-ins. `userhello` and `userbad` are narrow ring 3
  user-mode demos using the first `int 0x80` syscall ABI (`write`, `exit`,
  `read`, and `clock`). Arwill still does not
  have a general ELF loader, per-process address spaces, saved CPU contexts,
  independent kernel stacks, or preemptive user scheduling. Current cooperative
  yield saves explicit process progress only, not a hardware execution context.
- For the accepted `0.16.0` work, evolve only AWP ring 3 execution into the
  fixed four-slot model in ADR-0043. Keep cooperative kernel built-ins distinct;
  do not describe them as preemptive user processes.
- Treat Arwill as a single-owner OS. Do not introduce login accounts, groups,
  roles, or multi-user permission checks unless a later ADR explicitly changes
  this direction. The owner has full system control; ring 3 is an engineering
  guardrail for ordinary programs, not an account boundary.
- Treat ARFS v2 as a tiny mutable filesystem core with fixed entry, path, and
  file-size limits. It supports directory creation, whole-file byte writes,
  removal, and contiguous allocation, but has no append, rename, journal,
  atomic metadata update, or crash consistency. The shell exposes these
  operations through `mkdir`, `write`, `writehex`, and `rm`.
- Treat the current kernel heap as a small HHDM-backed free-list allocator. It
  is useful for small kernel objects, but it is not a slab allocator, virtual
  memory subsystem, userspace heap, or physical page release mechanism.
- Treat the current device registry as a fixed-size inspection table. It is not
  a full driver model, bus hierarchy, hotplug system, or ownership layer.
- Treat the current framebuffer text console as a serial-output mirror. It is
  not a graphics subsystem, terminal emulator, windowing layer, or input focus
  model.
- Treat the current clock as a PIT-backed monotonic millisecond counter with
  10 ms resolution. It is uptime since timer initialization, not RTC/CMOS
  calendar time, a date service, a timezone model, NTP, or process timers.
- Treat Arwill Program v1 as the first tiny stored executable format. It uses
  the `AWP1` magic, `.awp` extension, and `/apps` directory. It
  is not ELF, POSIX process loading, dynamic linking, argument passing,
  environment handling, or a per-process address-space model.
- Keep the `0.16.0` owner command surface minimal: `config`, `logs`, and
  `service` are the only new top-level shell commands; `ps` remains the one
  process inspection command. Do not add service enable/disable, log filters,
  or config show/get/set/reload aliases.
- Treat the TCP remote console as a plaintext, unauthenticated QEMU development
  interface bound to host localhost. It reuses the canonical shell dispatcher,
  supports one connection at a time, validates inbound checksums, and retains
  at most one output segment for bounded retransmission. It is not Telnet, SSH,
  a socket API, a full general-purpose TCP implementation, or safe for exposure
  to an untrusted network. SSH-specific crypto and host-key storage are
  intentionally absent per ADR-0042. Use the documented raw-terminal `stty`
  invocation for interactive `nc`; plain canonical-mode `nc` cannot deliver
  arrows, Tab, or Ctrl+C key-by-key.
- When a durable workflow agreement is made with the project owner, update this
  file or another appropriate document in the same change so future sessions do
  not need to rediscover it.
- Commit completed, verified milestones locally, but do not push every commit
  automatically. Push when asked, when sharing is needed, or when the owner has
  clearly approved publishing the accumulated work.
- For longer tasks, play `/System/Library/Sounds/Glass.aiff` with `afplay`
  after the work is complete, if tool permissions allow it.

Avoid implementing future subsystems as placeholders. A missing scheduler,
allocator, filesystem, shell, graphics layer, network stack, interrupt layer, or
driver model should remain honestly absent until there is a current requirement.

## Completion Report Format

Use this format when reporting completed work:

Summary

Files changed

Architectural impact

Commands executed

Verification result

Remaining risks
