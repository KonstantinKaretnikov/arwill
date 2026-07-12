# Initial Architecture

Arwill 0.7.0 has one executable path:

```text
Limine bootloader
  -> x86-64 Limine entry
  -> Limine memory map snapshot
  -> physical page allocator initialization
  -> QEMU serial I/O block
  -> QEMU ATA PIO block-device initialization
  -> ARFS filesystem mount from the raw test disk
  -> single-owner model publication
  -> x86-64 GDT, TSS, and user runtime initialization
  -> x86-64 IDT, PIC, and PIT timer initialization
  -> architecture-independent kernel startup
  -> cooperative kernel process manager initialization
  -> scheduler tick foundation initialization
  -> CPU interrupt enable
  -> serial shell
  -> storage-backed filesystem
  -> persistent owner-note write when write /owner/note is requested
  -> deterministic raw disk sector read when blkinfo is requested
  -> interrupt/timer diagnostics when irqinfo or irqprobe is requested
  -> scheduler tick diagnostics when schedinfo is requested
  -> cooperative built-in kernel process launch when run is requested
  -> yielded cooperative process continuation when step is requested
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

Input contract:

- Lives in `include/arwill/kernel/input.h`.
- Provides blocking byte input.
- It is not yet a general keyboard driver or event system.

Shell:

- Lives in `kernel/shell.c`.
- Owns command parsing for `help`, `version`, `pwd`, `cd`, `clear`, `ls`,
  `cat`, `write`, `stat`, `meminfo`, `blkinfo`, `irqinfo`, `irqprobe`,
  `schedinfo`, `userinfo`, `ownerinfo`, `ps`, `run`, `step`, `exit`, and
  `halt`.
- Keeps one canonical command name per operation; alias commands are not
  accepted.
- Holds the current working directory as local shell state.
- Owns Tab completion for command names, filesystem paths, and built-in process
  names.
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
- `ownerinfo` exposes this model in the shell.

Block device contract:

- Lives in `include/arwill/kernel/block_device.h`.
- Provides bounded sector reads and writes by LBA.
- The first block-device implementation has no block cache, partition table
  handling, request queue, DMA, or flush policy beyond the narrow ATA cache
  flush used after single-sector writes.
- The `blkinfo` shell command reads LBA 1 from the deterministic QEMU test disk
  image and prints a sample string.

Process manager:

- Public contract lives in `include/arwill/kernel/process.h`.
- Implementation lives in `kernel/process.c`.
- Owns a fixed-size table of kernel-managed processes with PID, state, run
  count, and exit code.
- The first scheduler behavior is cooperative: the shell can spawn a built-in
  kernel process with `run [name]`, then the process manager runs ready entries
  synchronously.
- Process entries can finish or yield. A yielded process returns to the ready
  state and can be continued with `step`; the first saved progress value is the
  process run count.
- Built-in `userhello` and `userbad` process entries enter ring 3 through the
  user runtime and return user exit status to this same process table.
- `ps` displays the process table.
- The process manager is still not a full scheduler. It does not own separate
  address spaces, ELF program loading, saved CPU contexts, independent kernel
  stacks, or preemptive context switching.

Interrupt controller contract:

- Public contract lives in `include/arwill/kernel/interrupts.h`.
- Architecture-independent helpers live in `kernel/interrupts.c`.
- The first x86-64 implementation lives in `arch/x86_64/cpu/interrupts.c`.
- It installs a minimal IDT, remaps the legacy PIC, unmasks IRQ0 only,
  configures the PIT at 100 Hz, handles breakpoint vector 3 for diagnostics,
  handles timer vector 32, and installs a DPL 3 `int 0x80` syscall gate.
- `irqinfo` reports IDT/PIC/PIT state and timer ticks. `irqprobe` triggers a
  safe breakpoint exception and verifies that vector 3 was handled.
- This is not a full interrupt subsystem: there is no APIC, IOAPIC, HPET,
  LAPIC timer, IRQ routing model, nested interrupt policy, or generic device
  interrupt registration yet.

Scheduler foundation:

- Public contract lives in `include/arwill/kernel/scheduler.h`.
- Implementation lives in `kernel/scheduler.c`.
- The PIT timer interrupt calls `arwill_scheduler_tick()`.
- The first implementation records timer ticks and alternates accounting
  between two named slots, `shell` and `idle`, so scheduler progress is visible
  through `schedinfo`.
- This is deliberately only a foundation. It does not save CPU contexts, switch
  stacks, preempt kernel code, wake sleeping tasks, or preempt user-space
  programs.

User runtime:

- Public contract lives in `include/arwill/kernel/user.h`.
- Architecture-independent wrappers live in `kernel/user.c`.
- The first x86-64 implementation lives in `arch/x86_64/cpu/user_mode.c`.
- Limine HHDM is requested so the kernel can initialize newly allocated
  physical pages before mapping them into user virtual memory.
- The x86-64 implementation installs a GDT with kernel and user descriptors,
  loads a TSS with an `rsp0` stack for privilege transitions, maps one user code
  page and one user stack page, and enters ring 3 with `iretq`.
- The first syscall ABI uses `int 0x80`: syscall `1` writes bytes to the serial
  console through the console contract, and syscall `2` exits with a status
  code.
- `run userhello` executes a tiny generated user program that writes
  `user hello: hello from ring 3` through syscall `write` and exits with code
  `7`.
- `run userbad` executes a tiny generated user program with an unknown syscall;
  the kernel converts it to exit code `127` and records a bad-syscall count.
- `userinfo` reports HHDM, GDT, TSS, syscall gate, run, syscall, byte, and
  bad-syscall counters.
- This is not a general program loader. There is no ELF loader, file-backed
  executable, per-process page table, argument passing, heap, signal model, or
  preemptive user scheduling yet.
- "User" here means CPU user mode, not an Arwill account model.

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

Filesystem contract:

- Lives in `include/arwill/kernel/filesystem.h`.
- Provides directory listing, whole-file reads by path, and a narrow whole-file
  overwrite operation.
- It does not yet provide open handles, streaming reads, allocation, append,
  delete, rename, directories creation, or mount behavior.

ARFS filesystem:

- Public mount entry lives in `include/arwill/kernel/arfs.h`.
- Implementation lives in `kernel/arfs.c`.
- Mounts from the current block device by reading a small ARFS superblock and
  manifest from the deterministic raw test disk image.
- Provides the primary filesystem for `ls`, `cd`, Tab completion, `cat`, and
  `stat` in the normal QEMU test path.
- Provides the first persistent write path for `/owner/note`. The note's data
  lives in a reserved data sector, and its size lives in a reserved ARFS state
  sector so the value survives a rebooted QEMU session.
- It is intentionally simple: fixed-size manifest parsing, one reserved
  writable text file, no general allocation, no arbitrary file creation, no
  append, no delete, no open handles, no block cache, and no partition table.

Static boot catalog:

- Lives in `kernel/boot_catalog.c`.
- Provides a tiny read-only directory tree for `ls`, `cd`, Tab completion,
  `cat`, and `stat`.
- Exposes small text payloads for `/system/identity` and
  `/boot/limine/limine.conf`; binary boot artifacts remain non-displayable.
- It is not a disk filesystem and does not read from storage.
- It now acts as a fallback if ARFS cannot mount.

QEMU ATA PIO block device:

- Lives in `platform/qemu/x86_64/ata_pio.c`.
- Uses legacy ATA PIO ports exposed by the QEMU `pc` machine type.
- Reads and writes sectors from the raw test disk image attached by the
  host-side run and smoke commands.
- It is intentionally a first storage path, not a general disk subsystem.

QEMU serial I/O:

- Lives in `platform/qemu/x86_64/`.
- Owns COM1 initialization, byte output, and byte input for the QEMU x86-64
  platform.
- Keeps x86-64 port I/O behind `arch/x86_64/include/`.

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
- The x86-64 boot block wires QEMU storage, ARFS, and the x86-64 interrupt
  controller and user runtime into the architecture-independent kernel entry.
- Limine config lives in `platform/qemu/limine.conf`.
- ISO construction is host-side development tooling, not kernel code.

## Dependency Direction

Architecture-independent kernel code depends on public contracts in
`include/arwill/kernel/`.

The x86-64 entry point wires the current platform implementation into the
kernel. Platform-specific code may use x86-64 primitives. Kernel orchestration
must not depend on QEMU or UART register details.
