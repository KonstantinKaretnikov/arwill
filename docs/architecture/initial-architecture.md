# Initial Architecture

Arwill 0.2.0 has one executable path:

```text
Limine bootloader
  -> x86-64 Limine entry
  -> Limine memory map snapshot
  -> physical page allocator initialization
  -> QEMU serial I/O block
  -> architecture-independent kernel startup
  -> QEMU ATA PIO block-device initialization
  -> cooperative kernel process manager initialization
  -> serial shell
  -> static read-only boot catalog
  -> deterministic raw disk sector read when blkinfo is requested
  -> cooperative built-in kernel process launch when run is requested
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
  `cat`, `stat`, `meminfo`, `blkinfo`, `ps`, `run`, `exit`, and `halt`.
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
  and CPU idle contracts.

Block device contract:

- Lives in `include/arwill/kernel/block_device.h`.
- Provides bounded sector reads by LBA.
- The first block-device implementation is read-only and has no block cache,
  partition table handling, write support, or filesystem parser.
- The `blkinfo` shell command reads LBA 1 from the deterministic QEMU test disk
  image and prints a sample string.

Process manager:

- Public contract lives in `include/arwill/kernel/process.h`.
- Implementation lives in `kernel/process.c`.
- Owns a fixed-size table of kernel-managed processes with PID, state, run
  count, and exit code.
- The first scheduler behavior is cooperative and run-to-completion: the shell
  can spawn a built-in kernel process with `run [name]`, then the process
  manager runs ready entries synchronously.
- `ps` displays the process table.
- This is not user space. There are no separate address spaces, ELF program
  loading, syscalls, kernel/user privilege transitions, timer interrupts, or
  preemptive context switching yet.

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

Filesystem contract:

- Lives in `include/arwill/kernel/filesystem.h`.
- Provides read-only directory listing and whole-file reads by path.
- It does not yet provide open handles, streaming reads, writes, allocation, or
  mount behavior.

Static boot catalog:

- Lives in `kernel/boot_catalog.c`.
- Provides a tiny read-only directory tree for `ls`, `cd`, Tab completion,
  `cat`, and `stat`.
- Exposes small text payloads for `/system/identity` and
  `/boot/limine/limine.conf`; binary boot artifacts remain non-displayable.
- It is not a disk filesystem and does not read from storage.

QEMU ATA PIO block device:

- Lives in `platform/qemu/x86_64/ata_pio.c`.
- Uses legacy ATA PIO ports exposed by the QEMU `pc` machine type.
- Reads sectors from the raw test disk image attached by the host-side run and
  smoke commands.
- It is intentionally a first storage read path, not a general disk subsystem.

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
- The x86-64 boot block requests the Limine memory map and converts it before
  entering architecture-independent kernel startup.
- Limine config lives in `platform/qemu/limine.conf`.
- ISO construction is host-side development tooling, not kernel code.

## Dependency Direction

Architecture-independent kernel code depends on public contracts in
`include/arwill/kernel/`.

The x86-64 entry point wires the current platform implementation into the
kernel. Platform-specific code may use x86-64 primitives. Kernel orchestration
must not depend on QEMU or UART register details.
