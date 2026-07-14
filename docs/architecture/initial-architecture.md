# Initial Architecture

Arwill 0.20.0 has one executable path:

```text
Limine bootloader
  -> x86-64 Limine entry
  -> Limine memory map snapshot
  -> physical page allocator initialization
  -> small kernel heap initialization from HHDM-mapped physical pages
  -> tiny device registry publication
  -> QEMU serial I/O block
  -> Limine framebuffer text console mirror
  -> QEMU ATA PIO system-disk initialization
  -> bounded block-device region at LBA 32768
  -> ARFS filesystem mount from that region
  -> owner configuration and event-log initialization
  -> single-owner model publication
  -> QEMU e1000 initialization and supervisor-only MMIO mapping
  -> x86-64 GDT, TSS, and user runtime initialization
  -> x86-64 IDT, PIC, and PIT timer initialization
  -> architecture-independent kernel startup
  -> stackful cooperative kernel task manager initialization
  -> kernel/user scheduler accounting initialization
  -> CPU interrupt enable
  -> network-poll and remote-console system task creation
  -> serial shell and authenticated TCP remote-console service
  -> storage-backed filesystem
  -> persistent whole-file writes through AWP syscalls and internal smoke commands
  -> grouped system, device, and network inspection commands
  -> bounded filesystem allocation statistics through system storage
  -> live per-session system and process dashboard when top is requested
  -> kernel heap diagnostics through system memory and an internal heap smoke probe
  -> bounded ARFS-region sector read through devices disk0
  -> interrupt/timer diagnostics through system interrupts
  -> scheduler tick diagnostics through system scheduler
  -> cooperative built-in kernel process launch when run is requested
  -> saved kernel task context continuation through an internal smoke command
  -> stored Arwill Program spawn into one of four AWP slots when exec is requested
  -> user page mapping and built-in ring 3 user program launch when userhello
     or userbad is run
  -> QEMU debug-exit poweroff when exit is requested
  -> x86-64 CPU idle loop when halt is requested
```

## Block Boundaries

Kernel entry:

- Lives in `kernel/`.
- Owns startup orchestration.
- Emits status only through the console contract.
- Does not manipulate UART registers or QEMU-specific devices.

Console contract:

- Lives in `include/arwill/kernel/console.h`.
- Provides only `write` and `write_line`.
- Is intentionally smaller than a driver model or formatting library.

Framebuffer text console:

- Public init contract lives in
  `arch/x86_64/include/arwill/arch/x86_64/framebuffer_console.h`.
- Implementation lives in `arch/x86_64/boot/framebuffer_console.c`.
- Uses Limine's first 32-bit framebuffer when available.
- Mirrors the serial console with a small built-in 5x7 bitmap font.
- Handles printable ASCII, newlines, carriage returns, and backspace well
  enough for current shell output.
- It is not a graphics subsystem: there is no windowing, color theme, font
  loading, acceleration, scrolling buffer, or input focus.

Input contract:

- Lives in `include/arwill/kernel/input.h`.
- Provides blocking byte input and nonblocking byte polling.
- It is not yet a general keyboard driver or event system.

Shell:

- Lives in `kernel/shell.c`.
- Owns the canonical user command table, including filesystem, diagnostic,
  process, network, `config`, `logs`, and `service` operations.
- Retains exact-match internal smoke commands outside `help` and Tab completion.
- Keeps one canonical public command name per operation; retired exact-match
  diagnostics are transitional internal inputs, not public aliases.
- Holds the current working directory as local shell state.
- Owns Tab completion for command names, filesystem paths, short `/apps`
  program names, both `exec` positions, built-in process names, and fixed
  subsystem arguments.
- Keeps `top` as nonblocking per-session shell state. The main service loop
  refreshes it once per second while system-task and AWP dispatch continue.
- Starts `network-poll` and `remote-console` as automatic system tasks. The
  first polls one bounded TCP/network pass; the second owns connection,
  authentication, remote input, and remote shell-session progress.
- Owns a small in-memory command history navigated by Up and Down escape
  sequences.
- Normalizes standard Russian-layout UTF-8 input back to ASCII key positions;
  it does not support Cyrillic text entry yet.
- Depends on block device, console, input, filesystem, memory, process, power,
  interrupts, scheduler, user runtime, and CPU idle contracts.

Ownership model:

- Arwill is a single-owner OS.
- There are no login accounts, role databases, user IDs, groups, or multi-user
  permission checks.
- The owner has full system control by design.
- The kernel/user CPU boundary is retained as an engineering boundary: ordinary
  ring 3 programs use syscalls, while privileged access is added deliberately as
  kernel or driver work.
- `system owner` exposes this model in the shell.

Device registry:

- Public contract lives in `include/arwill/kernel/device.h`.
- Implementation lives in `kernel/device.c`.
- The registry is a small fixed-size inspection table for detected devices and
  published contracts.
- The first entries include serial console/input, ARFS or fallback filesystem,
  QEMU ATA block device, kernel heap, timer/interrupts, QEMU poweroff, and user
  runtime.
- `devices` lists name, kind, driver, and status.
- This is not a full driver model: devices are not hot-pluggable, there is no
  bus hierarchy, no dynamic probing policy, and no ownership transfer.

Block device contract:

- Lives in `include/arwill/kernel/block_device.h`.
- Provides bounded sector reads and writes by LBA.
- Can expose a caller-owned bounded region whose LBAs are translated into a
  parent block device and cannot escape the configured range.
- The first block-device implementation has no block cache, partition table
  handling, request queue, DMA, or flush policy beyond the narrow ATA cache
  flush used after single-sector writes.
- `devices disk0` reads LBA 1 from the bounded ARFS region and prints its
  internal diagnostic sample string.

Process manager:

- Public contract lives in `include/arwill/kernel/process.h`.
- Implementation lives in `kernel/process.c`.
- Owns a fixed-size table of kernel-managed tasks with PID, state, run count,
  exit code, saved context, and one preallocated 8 KiB stack per slot.
- Distinguishes automatically dispatched `system` tasks from manually
  launched `kernel` built-ins. Both use the same stackful cooperative context
  contract; AWP remains a third, separate task model.
- The scheduler behavior is cooperative: the shell can spawn a built-in kernel
  task with `run [name]`, then the process manager resumes each ready task once
  per dispatch pass.
- `counter` is the only cooperative kernel built-in. The obsolete
  run-to-completion `hello` demonstration is not part of the current surface.
- Process entries can finish or explicitly call `arwill_process_yield`. A
  yielded task returns to the ready state; its stack, local variables, call
  chain, stack pointer, and x86-64 callee-saved registers remain intact until
  the next dispatch resumes immediately after the yield call.
- Built-in `userhello` and `userbad` process entries enter ring 3 through the
  user runtime and return user exit status to this same process table.
- `ps` displays cooperative kernel entries and the separate AWP task table;
  `top` renders both in one live table with an explicit process-kind column.
- The architecture-independent process manager receives a context backend at
  boot. The x86-64 backend saves the stack pointer and ABI callee-saved
  registers at cooperative switch boundaries.
- Each dispatch pass uses a scoped scheduler context. This permits a remote
  system task to dispatch a manual kernel built-in through the canonical shell
  without losing the outer system-task continuation.
- Kernel tasks share one kernel address space. They have no stack guards and
  are never preempted; AWP full ring 3 contexts, address spaces, and PIT
  preemption remain a separate user-runtime mechanism.

Interrupt controller contract:

- Public contract lives in `include/arwill/kernel/interrupts.h`.
- Architecture-independent helpers live in `kernel/interrupts.c`.
- The first x86-64 implementation lives in `arch/x86_64/cpu/interrupts.c`.
- It installs a minimal IDT, remaps the legacy PIC, unmasks IRQ0 only,
  configures the PIT at 100 Hz, handles breakpoint vector 3 for diagnostics,
  handles timer vector 32, and installs a DPL 3 `int 0x80` syscall gate.
- `system interrupts` reports IDT/PIC/PIT state and timer ticks. An internal
  smoke probe triggers a safe breakpoint exception and verifies vector 3.
- This is not a full interrupt subsystem: there is no APIC, IOAPIC, HPET,
  LAPIC timer, IRQ routing model, nested interrupt policy, or generic device
  interrupt registration yet.

Clock contract:

- Public contract lives in `include/arwill/kernel/clock.h`.
- The x86-64 implementation converts the 100 Hz PIT counter to monotonic
  milliseconds with 10 ms resolution.
- `system` and user syscall `4` expose the same uptime value.
- RTC/CMOS calendar time, dates, timezones, NTP, and process timers remain
  absent.

PCI discovery:

- The architecture-independent fixed PCI table lives in
  `include/arwill/kernel/pci.h`; x86-64 configuration access lives in
  `arch/x86_64/cpu/pci.c`.
- `devices pci` scans bus 0 with configuration mechanism #1 and exposes vendor,
  device, class, and raw BAR values.
- The QEMU e1000 driver consumes its known BAR and maps MMIO in a dedicated
  supervisor-only high-half range. There is still no general PCI resource
  allocator, IRQ/MSI setup, hotplug, or driver binding model.

Scheduler accounting:

- Public contract lives in `include/arwill/kernel/scheduler.h`.
- Implementation lives in `kernel/scheduler.c`.
- The PIT timer interrupt calls `arwill_scheduler_tick()`.
- It records kernel and user timer ticks, while the x86-64 user runtime applies
  a fixed two-tick quantum to AWP execution.
- It does not preempt kernel code, switch kernel stacks, implement priorities,
  or provide SMP scheduling.

User runtime:

- Public contract lives in `include/arwill/kernel/user.h`.
- Architecture-independent wrappers live in `kernel/user.c`.
- The first x86-64 implementation lives in `arch/x86_64/cpu/user_mode.c`.
- Limine HHDM is requested so the kernel can initialize newly allocated
  physical pages before mapping them into user virtual memory.
- The x86-64 implementation installs a GDT and TSS, preallocates four AWP
  slots, and gives each slot its own CR3, two read/execute code pages, two
  read/write non-executable stack pages, and an unmapped stack guard.
- The syscall ABI uses `int 0x80`: syscall `1` writes to the originating
  session, syscall `2` exits, syscall `3` reads session input, syscall `4`
  returns monotonic milliseconds, and syscalls `5` and `6` perform bounded
  whole-text-file reads and writes. Syscall `7` copies the task's one bounded,
  shell-resolved launch file path to a user buffer.
- `run userhello` executes a tiny generated user program that writes
  `user hello: hello from ring 3` through syscall `write` and exits with code
  `7`.
- `run userbad` executes a tiny generated user program with an unknown syscall;
  the kernel converts it to exit code `127` and records a bad-syscall count.
- `system runtime` reports HHDM, GDT, TSS, syscall gate, run, syscall, byte,
  and bad-syscall counters.
- AWP execution saves the complete ring 3 context, round-robins ready tasks,
  blocks only the calling task for input, and contains user invalid-opcode,
  general-protection, and page faults.
- This is not a general process model. There is no ELF loader, demand paging,
  argument vector, userspace heap, signal model, fork, independent kernel
  stacks, kernel preemption, or SMP.
- "User" here means CPU user mode, not an Arwill account model.

Program loader:

- The first stored executable format is Arwill Program v1, identified by an
  `AWP1` binary header.
- The shell command `exec [program] [file]` reads a binary file from ARFS and asks
  the user runtime to map and execute its code bytes in ring 3 with at most one
  63-byte launch file path resolved against the shell current directory.
- A bare program name without `.awp` resolves exactly to `/apps/<name>.awp`.
  Image arguments containing `/`, or ending in `.awp`, retain ordinary explicit
  filesystem-path resolution. There is no `PATH` or multi-directory search.
- The current image format contains a small header, entry offset, and at most
  two code pages.
- The fixture packages `/apps/hello.awp`, `/apps/calc.awp`, and
  `/apps/edit.awp`.
- This is deliberately not ELF: there is no linker, relocation, dynamic
  loading, general `argc`/`argv`, quoting, environment, or file descriptors.

Power contract:

- Lives in `include/arwill/kernel/power.h`.
- Provides a narrow `poweroff` operation.
- The first implementation is QEMU-specific and uses `isa-debug-exit`.
- If the current platform cannot power off, the kernel falls back to the CPU
  idle loop.

Memory contract:

- Lives in `include/arwill/kernel/memory.h`.
- Provides a boot memory map snapshot using Arwill-owned region types.
- Provides physical page allocator counters and page allocation from usable
  memory ranges.
- The first allocator is bump-only: it can allocate pages but cannot free or
  reuse pages yet.
- User-mode setup consumes physical pages for generated user code, user stack,
  and page-table pages; those allocations are not freed yet.
- The first kernel heap reserves a small contiguous run of physical pages,
  accesses them through Limine HHDM, and manages them with a simple free list.
- The kernel heap can allocate and free small kernel objects, split free blocks,
  coalesce adjacent free blocks, and report counters through `system memory`.
- This is not a general virtual memory subsystem: there is no paging policy,
  demand mapping, slab cache, userspace heap, or physical page release path yet.

Filesystem contract:

- Lives in `include/arwill/kernel/filesystem.h`.
- Provides directory listing, whole-file reads and writes by path, directory
  creation, and removal.
- Optionally reports bounded storage allocation statistics through a single
  snapshot operation. Unsupported filesystems report the statistics as
  unavailable.
- It does not provide open handles, streaming, append, seek, rename, or atomic
  metadata updates.

ARFS filesystem:

- Public mount entry lives in `include/arwill/kernel/arfs.h`.
- Implementation lives in `kernel/arfs.c`.
- Mounts from the bounded system-disk region by reading a small ARFS
  superblock and manifest at region-relative LBAs.
- Provides the primary filesystem for `ls`, `cd`, Tab completion, `cat`, and
  `stat` in the normal QEMU test path.
- Persists bounded catalog mutations and contiguous data allocation across a
  rebooted QEMU session.
- Reports its fixed entry capacity, data-sector use, largest free contiguous
  run, manifest size, and path/file limits through `system storage`.
- It is intentionally simple: a fixed 24-entry table, short paths, 8192-byte
  file limit, no append, rename, journal, atomic metadata update, open handles,
  block cache, or partition table.

Static boot catalog:

- Lives in `kernel/boot_catalog.c`.
- Provides a tiny read-only directory tree for `ls`, `cd`, Tab completion,
  `cat`, and `stat`.
- Exposes small text payloads for `/system/identity` and
  `/boot/limine/limine.conf`; binary boot artifacts remain non-displayable.
- It is not a disk filesystem and does not read from storage.
- It now acts as a fallback if ARFS cannot mount.

ARFS mutable core:

- ARFS v2 stores its fixed-size mutable catalog in a two-sector text manifest.
- It supports directory creation, whole-file text or binary writes, removal,
  and contiguous allocation for files of at most 8192 bytes.
- Free ranges are inferred from catalog entries; there is no bitmap, journal,
  cache, append, rename, or crash-consistency model.
- The user shell exposes directory creation and removal through `mkdir` and
  `rm`; `/apps/edit.awp` uses bounded whole-text-file AWP syscalls. Exact-match
  internal smoke commands retain text and binary whole-file fixture writes.

QEMU ATA PIO block device:

- Lives in `platform/qemu/x86_64/ata_pio.c`.
- Uses legacy ATA PIO ports exposed by the QEMU `pc` machine type.
- Probes master and slave positions on both the primary and secondary legacy
  IDE channels, then exposes the first ATA disk it identifies.
- Reads and writes the one bootable raw system-disk image attached by the
  host-side run and smoke commands.
- The architecture-independent region wrapper exposes only LBA 32768 through
  LBA 34815 to ARFS, so allocation cannot overwrite Limine or the kernel.
- It is intentionally a first storage path, not a general disk subsystem.

QEMU e1000 network device:

- Reads the backend-configured unicast MAC from e1000 RAL/RAH after reset and
  uses that address for receive filtering and generated Ethernet frames.
- Does not assume the deterministic MAC used by the repository's normal QEMU
  launch path; UTM may assign a different address to the same emulated device.

QEMU serial I/O:

- Lives in `platform/qemu/x86_64/`.
- Owns COM1 initialization, byte output, and byte input for the QEMU x86-64
  platform.
- Keeps x86-64 port I/O behind `arch/x86_64/include/`.

Configuration, event log, and service:

- `/owner/arwill.conf` is a strict versioned five-key ASCII file exposed by
  the single `config` command; secret input is never echoed.
- `kernel/log.c` retains 64 structured in-memory lifecycle events and `logs`
  prints the complete chronological ring.
- `kernel/service.c` owns the one built-in `remote-console` service with
  running, stopped, failed, and unavailable states.
- Authentication is a three-attempt access gate for trusted networks, not
  encrypted transport or a multi-user login model.

QEMU e1000 and remote output:

- The e1000 driver maps its BAR in a dedicated supervisor-only high-half range
  before AWP address spaces inherit kernel mappings.
- TCP retains one segment for bounded retransmission. A fixed transmit byte
  ring prevents an AWP `write` syscall from waiting for a peer ACK.
- The path remains one polling connection, not a socket API or general TCP
  implementation.

QEMU poweroff:

- Lives in `platform/qemu/x86_64/power.c`.
- Uses the QEMU `isa-debug-exit` device on port `0xf4`.
- Host-side QEMU launch commands add that device and treat its expected exit
  status as successful shell poweroff.

CPU idle:

- Public contract lives in `include/arwill/kernel/cpu.h`.
- x86-64 implementation lives in `arch/x86_64/cpu/`.
- The kernel can request a controlled idle state without embedding the `hlt`
  instruction.

Boot infrastructure:

- Limine is fetched into `third_party/limine/`.
- The x86-64 boot block requests the Limine memory map and HHDM offset before
  entering architecture-independent kernel startup.
- The x86-64 boot block wires QEMU storage, ARFS, configuration, e1000,
  user-address-space initialization, interrupts, and the built-in service into
  the architecture-independent kernel entry in that dependency order.
- Limine config lives in `platform/qemu/limine.conf`.
- ISO construction is host-side development tooling, not kernel code.

## Dependency Direction

Architecture-independent kernel code depends on public contracts in
`include/arwill/kernel/`.

The x86-64 entry point wires the current platform implementation into the
kernel. Platform-specific code may use x86-64 primitives. Kernel orchestration
must not depend on QEMU or UART register details.
