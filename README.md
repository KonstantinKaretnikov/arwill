# Arwill

Arwill is an experimental operating system designed through continuous
collaboration between a human architect and AI coding agents.

The central statement of the project is:

> The architecture is the product.

All Arwill-owned source code is generated from human requirements,
architectural discussions, reviews, and decisions. AI-generated code is still
expected to be reviewed, tested, and understood.

Arwill is an early experimental project, not a production operating system.

## Current Status

Version: `0.14.0`

The current milestone boots an x86-64 kernel in QEMU through Limine, writes
initialization status to the serial console, and starts a tiny serial shell.
The shell can read terminal keyboard input through QEMU serial I/O, inspect a
boot memory map, mirror serial output to a framebuffer text console, report
physical page allocator and kernel heap state, list the detected device
registry, and launch small cooperative kernel processes that can yield and
continue on later shell steps. It can also load a tiny stored Arwill Program
Image from ARFS and run it in ring 3. Arwill can also read sectors from a
QEMU-attached raw test disk through an ATA PIO block-device driver and serve
shell filesystem commands from a storage-backed ARFS image. ARFS v2 now exposes
bounded directory creation, whole-file text and binary writes, removal, and
reuse of released contiguous space.
The x86-64/QEMU path now installs an IDT, remaps the legacy PIC, configures the
PIT timer, and exposes interrupt, timer, and scheduler tick diagnostics. It can
also enter ring 3 for tiny built-in user programs, handle `int 0x80` syscalls
for output, exit, serial input, and monotonic time, and report their exit status
through `ps`.

Arwill is a single-owner OS: there are no login accounts, roles, or multi-user
permission model. The owner has full system control, while the kernel/user
boundary remains as an engineering guardrail around ordinary ring 3 programs.

## Supported Host and Target

- Host: macOS with Homebrew
- Target architecture: x86-64
- Target platform: QEMU
- Languages: freestanding C and isolated x86-64 inline assembly
- Bootloader: Limine, fetched as pinned third-party infrastructure

## macOS Prerequisites

Install only the tools used by this repository:

```sh
brew install llvm lld xorriso qemu
```

Verify them:

```sh
/opt/homebrew/opt/llvm/bin/clang --version
/opt/homebrew/opt/lld/bin/ld.lld --version
xorriso -version
qemu-system-x86_64 --version
```

More setup detail is in `docs/development/macos.md`.

## Build

Fetch pinned Limine infrastructure:

```sh
make setup
```

Build the kernel and bootable ISO:

```sh
make build
```

## Run

Launch QEMU with serial output attached to the terminal:

```sh
make run
```

Stop QEMU with `Ctrl-C`.

Available shell commands:

```text
help
version
uptime
pciinfo
pwd
cd [path]
clear
ls [path]
cat [path]
mkdir [path]
write [path] [text]
writehex [path] [hex]
rm [path]
stat [path]
meminfo
heaptest
devices
blkinfo
irqinfo
irqprobe
schedinfo
userinfo
ownerinfo
ps
run [name]
exec [path]
step
exit
halt
```

`ls` lists the current filesystem. In the normal QEMU test path this is ARFS
mounted from the raw test disk image. The supported paths are `/`, `/boot`,
`/boot/limine`, `/docs`, `/owner`, and `/system`. `cd` changes the shell's
current directory and supports absolute paths, relative paths, `.`, and `..`.

`cat` displays text files from ARFS, such as `/system/identity`,
`/boot/limine/limine.conf`, and `/docs/readme`. Binary boot artifacts are
visible in listings, but their contents are not displayed yet.

`mkdir` creates a directory, `write` creates or replaces a complete text file,
`writehex` creates or replaces a binary file from pairs of hexadecimal digits,
and `rm` removes a file or empty directory. Mutations are persisted to the QEMU
raw disk image. ARFS v2 remains bounded to 16 entries, short paths, files below
2048 bytes, and contiguous allocation; it has no append, rename, journal, or
crash consistency.

`stat` displays directory and file metadata. `devices` lists the current
platform devices and their basic status. `meminfo` prints the
Limine-provided boot memory map, physical page allocator counters, and kernel
heap counters. `heaptest` allocates and frees two small heap blocks.

`blkinfo` displays the detected QEMU ATA PIO block device, its sector geometry,
and a sample string read from LBA 1 of the deterministic test disk image.

`irqinfo` displays the x86-64 interrupt setup status and PIT timer tick count.
`irqprobe` triggers a safe breakpoint exception and verifies that vector 3 is
handled. `schedinfo` displays the current timer tick accounting for the first
scheduler foundation.

`uptime` displays monotonic milliseconds since PIT initialization. The same
value is available to AWP programs through syscall `4` (`clock`). This is not
calendar time: Arwill still has no RTC/CMOS reader, date, timezone, or NTP.

`pciinfo` lists the bounded PCI scan used to discover platform devices. The
current QEMU path attaches an Intel e1000 NIC. `netinfo` reports its fixed
diagnostic MAC and `netprobe` transmits a bounded broadcast Ethernet frame;
ARP, IP, sockets, and SSH are not implemented yet.

`netcfg` shows the fixed QEMU user-network address (`10.0.2.15/24`) and
gateway (`10.0.2.2`). `arping` constructs and transmits an ARP request for
that gateway. `ping` completes one bounded ARP/ICMP echo exchange with the
QEMU gateway. DHCP, routing, TCP, sockets, and SSH are still planned.

Interactive `make run` forwards host `127.0.0.1:22223` to guest TCP port 22.
The current listener performs only the TCP handshake; it has no SSH payload
service yet.

`userinfo` displays the current x86-64 user-mode setup, including HHDM, GDT,
TSS, syscall-gate, run, syscall, byte, and bad-syscall counters.

`ownerinfo` displays Arwill's single-owner model: no accounts, full owner
control, ordinary ring 3 programs through syscalls, and privileged work through
explicit kernel or driver code.

`run [name]` launches one of the built-in cooperative kernel processes:
`hello`, `counter`, `userhello`, or `userbad`. `counter` yields between its
three visible steps and can be continued with `step`. `userhello` enters ring 3
and prints through the `write` syscall before exiting with code `7`. `userbad`
enters ring 3 and makes an unknown syscall, which exits with code `127`
without crashing the kernel. `ps` shows the process table with PID, state, run
count, exit code, and name.

`exec /apps/hello.awp` loads a tiny Arwill Program from ARFS and runs it through
the same ring 3 `int 0x80` syscall boundary. AWP is deliberately
small and is not ELF, POSIX, dynamic linking, arguments, or environment
support.

`exec /apps/calc.awp` runs the deliberately plain interactive calculator. Type
one expression such as `12*7` and press Enter. It supports integer `+`, `-`,
`*`, and `/`; division by zero and malformed expressions print `error`.

The test application build recipes live in `apps/hello/` and `apps/calc/`; their
outputs are packaged into the ARFS test disk as `.awp` files.

These are still narrow built-in programs. Arwill does not yet have ELF program
loading, per-process address spaces, saved CPU contexts, or preemptive context
switching.

The QEMU path mirrors serial output to a simple framebuffer text console when
Limine provides a 32-bit framebuffer. `exit` powers off the current QEMU
session. `halt` remains available as a CPU idle-loop command.

Press `Tab` to complete command names, paths, and built-in process names. If
there are multiple matches, the shell lists candidates and redraws the current
prompt. Press Up and Down to browse the in-memory shell command history.

If the host terminal is left in a Russian keyboard layout, the shell normalizes
standard Russian-layout UTF-8 input back to ASCII key positions for commands and
paths. This is an input convenience only; Cyrillic text entry is not supported
yet.

## Check

Run all available verification, including the bounded QEMU serial smoke test:

```sh
make check
```

## Expected Serial Output

```text
Arwill 0.14.0
architecture: x86_64
platform: qemu
console: serial
input: serial
owner: single-owner
shell: ready
filesystem: arfs mutable
block: qemu ata pio
memory: boot memory map
allocator: physical page bump allocator + kernel heap
devices: registry
processes: kernel cooperative
interrupts: x86_64 idt pic pit
scheduler: timer tick foundation
user: x86_64 ring3 int80
power: qemu debug exit
status: kernel initialized
Arwill:/>
```

## Repository Map

- `AGENTS.md`: durable guidance for future AI coding agents.
- `MANIFESTO.md`: project principles.
- `docs/roadmap.md`: agreed order for upcoming large milestones.
- `docs/architecture/`: architecture notes for the current system.
- `docs/decisions/`: architectural decision records.
- `docs/development/`: host setup and development workflows.
- `include/`: public Arwill-owned C contracts.
- `kernel/`: architecture-independent kernel orchestration, shell, contracts,
  static boot catalog fallback, ARFS filesystem support, and user-runtime
  wrappers.
- `arch/x86_64/`: x86-64 entry, CPU idle, interrupt setup, port I/O, and linker
  details, including the first ring 3 user-mode path.
- `platform/qemu/`: QEMU-specific platform wiring and serial console block.
- `scripts/`: host-side setup, artifact checks, and boot smoke tests.
- `third_party/`: documented external dependencies fetched by `make setup`.
- `build/`: generated artifacts, ignored by git.

## External Code

Limine is external boot infrastructure, not Arwill-owned source code. It is
fetched into `third_party/limine/` by `make setup`, verified by SHA-256, and
excluded from git. See `third_party/README.md` and ADR-0004.
