# Initial Architecture

Arwill 0.0.1 has one executable path:

```text
Limine bootloader
  -> x86-64 Limine entry
  -> QEMU serial console block
  -> architecture-independent kernel startup
  -> x86-64 CPU idle loop
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

QEMU serial console:

- Lives in `platform/qemu/x86_64/`.
- Owns COM1 initialization and byte output for the QEMU x86-64 platform.
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
