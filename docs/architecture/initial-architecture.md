# Initial Architecture

Arwill 0.0.2 has one executable path:

```text
Limine bootloader
  -> x86-64 Limine entry
  -> QEMU serial I/O block
  -> architecture-independent kernel startup
  -> serial shell
  -> static read-only boot catalog
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
- Owns command parsing for `help`, `version`, `ls`, `dir`, and `halt`.
- Depends on console, input, filesystem, and CPU idle contracts.

Filesystem contract:

- Lives in `include/arwill/kernel/filesystem.h`.
- Provides read-only directory listing by path.
- It does not yet provide open, read, write, allocation, or mount behavior.

Static boot catalog:

- Lives in `kernel/boot_catalog.c`.
- Provides a tiny read-only directory tree for the first `ls` command.
- It is not a disk filesystem and does not read from storage.

QEMU serial I/O:

- Lives in `platform/qemu/x86_64/`.
- Owns COM1 initialization, byte output, and byte input for the QEMU x86-64
  platform.
- Keeps x86-64 port I/O behind `arch/x86_64/include/`.

CPU idle:

- Public contract lives in `include/arwill/kernel/cpu.h`.
- x86-64 implementation lives in `arch/x86_64/cpu/`.
- The kernel can request a controlled idle state without embedding the `hlt`
  instruction.

Boot infrastructure:

- Limine is fetched into `third_party/limine/`.
- Limine config lives in `platform/qemu/limine.conf`.
- ISO construction is host-side development tooling, not kernel code.

## Dependency Direction

Architecture-independent kernel code depends on public contracts in
`include/arwill/kernel/`.

The x86-64 entry point wires the current platform implementation into the
kernel. Platform-specific code may use x86-64 primitives. Kernel orchestration
must not depend on QEMU or UART register details.
