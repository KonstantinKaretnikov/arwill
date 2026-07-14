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
  `read`, and `clock`). Stored AWP programs use the fixed four-slot model in
  ADR-0043: saved ring 3 contexts, preallocated per-slot address spaces,
  PIT preemption, round-robin dispatch, session-bound input, and user-fault
  containment. Cooperative kernel built-ins remain distinct and do not save a
  hardware execution context. Arwill still has no ELF loader, independent
  kernel stacks, kernel preemption, or SMP.
- The AWP syscall ABI also includes bounded whole-text-file `read_file` and
  `write_file` operations for current consumers such as `/apps/edit.awp`.
  These are not file descriptors, streams, append, seek, or a POSIX file API.
- Treat Arwill as a single-owner OS. Do not introduce login accounts, groups,
  roles, or multi-user permission checks unless a later ADR explicitly changes
  this direction. The owner has full system control; ring 3 is an engineering
  guardrail for ordinary programs, not an account boundary.
- Treat ARFS v2 as a tiny mutable filesystem core with fixed entry, path, and
  file-size limits. It supports directory creation, whole-file byte writes,
  removal, and contiguous allocation, but has no append, rename, journal,
  atomic metadata update, or crash consistency. The user shell exposes
  `mkdir` and `rm`; `/apps/edit.awp` is the user-facing ASCII text editor.
  `write` and `writehex` remain exact-match internal smoke commands.
- Keep `netprobe`, `arping`, `tcpcheck`, `tcplisten`, `heaptest`, `irqprobe`,
  `step`, `write`, and `writehex` available only as exact-match internal smoke
  commands. Do not list them in `help`, include them in Tab completion, or
  document them as user commands unless a later ADR changes this decision.
- Keep the retired `uptime`, `pciinfo`, `netinfo`, `netcfg`, `ping`, `tcpinfo`,
  `meminfo`, `blkinfo`, `irqinfo`, `schedinfo`, `userinfo`, and `ownerinfo`
  entry points hidden from `help`, Tab completion, and user documentation.
  They are temporary exact-match diagnostic inputs during the `0.17.0`
  transition; do not build new consumers on them.
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
  the `AWP1` magic, `.awp` extension, and `/apps` directory. `exec` may pass
  exactly one optional file path of at most 63 bytes. Both image and file paths
  support Tab completion; the shell resolves a relative file against its
  current directory before syscall `7` exposes the canonical path to the AWP.
  A bare image name without `.awp` resolves only to `/apps/<name>.awp`; explicit
  image paths retain normal filesystem resolution. This is not `PATH`, an alias
  layer, or a general program search mechanism.
  This is not `argc`/`argv`, quoting, environment handling, inherited working-
  directory state inside the task, ELF, POSIX process loading, dynamic linking,
  or a dynamic virtual-memory ABI.
- Keep the `0.17.x` inspection surface grouped under `system`, `devices`, and
  `network`. `top` is the live per-session dashboard; `ps` remains the stable
  one-shot process listing. Preserve the `KIND` distinction between cooperative
  kernel built-ins and scheduled AWP tasks. Do not add service enable/disable,
  log filters, or config show/get/set/reload aliases.
- Treat `system storage` as bounded filesystem-reported allocation statistics.
  For ARFS it reports fixed entry usage, data-sector usage, the largest free
  contiguous run, manifest sectors, and current path/file limits. It is not a
  quota system, partition inspector, generic disk-capacity API, or promise of
  crash-consistent free-space accounting.
- Treat the TCP remote console as a plaintext, access-key-gated QEMU
  development service bound to host localhost by default. It reuses the
  canonical shell dispatcher, supports one connection at a time, validates
  inbound checksums, retains at most one segment for bounded retransmission,
  and queues bounded output so an AWP syscall never waits for a peer ACK. The
  explicit `QEMU_REMOTE_CONSOLE_BIND=0.0.0.0` override is for a trusted LAN
  only; the key and shell traffic are still observable. It is not Telnet, SSH,
  TLS, a socket API, a full general-purpose TCP implementation, or safe for
  Internet exposure. SSH-specific crypto and host-key storage remain absent.
  Use the documented raw-terminal `stty` invocation for interactive `nc`;
  plain canonical-mode `nc` cannot deliver arrows, Tab, or Ctrl+C key-by-key.
- Initialize supervisor-only platform MMIO mappings before AWP address spaces
  copy kernel PML4 entries. Keep device MMIO in a dedicated high-half range,
  separate from Limine HHDM. See ADR-0047.
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
