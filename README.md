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

Version: `0.3.0`

The current milestone boots an x86-64 kernel in QEMU through Limine, writes
initialization status to the serial console, and starts a tiny serial shell.
The shell can read terminal keyboard input through QEMU serial I/O, inspect a
boot memory map, report the first physical page allocator state, and launch
small cooperative kernel processes. Arwill can also read sectors from a
QEMU-attached raw test disk through a read-only ATA PIO block-device driver and
serve shell filesystem commands from a storage-backed read-only ARFS image.

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
pwd
cd [path]
clear
ls [path]
cat [path]
stat [path]
meminfo
blkinfo
ps
run [name]
exit
halt
```

`ls` lists the current read-only filesystem. In the normal QEMU test path this
is ARFS mounted from the raw test disk image. The supported paths are `/`,
`/boot`, `/boot/limine`, `/docs`, and `/system`. `cd` changes the shell's
current directory and supports absolute paths, relative paths, `.`, and `..`.

`cat` displays text files from ARFS, such as `/system/identity`,
`/boot/limine/limine.conf`, and `/docs/readme`. Binary boot artifacts are
visible in listings, but their contents are not displayed yet.

`stat` displays directory and file metadata. `meminfo` prints the
Limine-provided boot memory map and the current physical page allocator
counters.

`blkinfo` displays the detected QEMU ATA PIO block device, its sector geometry,
and a sample string read from LBA 1 of the deterministic test disk image. This
proves sector reads only; shell filesystem commands still use the static boot
catalog.

`run [name]` launches one of the built-in cooperative kernel processes:
`hello` or `counter`. `ps` shows the kernel process table with PID, state, run
count, exit code, and name. These are kernel-managed processes that run to
completion; Arwill does not have user-space isolation, ELF program loading,
syscalls, or preemptive scheduling yet.

`exit` powers off the current QEMU session. `halt` remains available as a CPU
idle-loop command.

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
Arwill 0.3.0
architecture: x86_64
platform: qemu
console: serial
input: serial
shell: ready
filesystem: arfs read-only disk
block: qemu ata pio
memory: boot memory map
allocator: physical page bump allocator
processes: kernel cooperative
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
  static boot catalog fallback, and ARFS read-only filesystem support.
- `arch/x86_64/`: x86-64 entry, CPU idle, port I/O, and linker details.
- `platform/qemu/`: QEMU-specific platform wiring and serial console block.
- `scripts/`: host-side setup, artifact checks, and boot smoke tests.
- `third_party/`: documented external dependencies fetched by `make setup`.
- `build/`: generated artifacts, ignored by git.

## External Code

Limine is external boot infrastructure, not Arwill-owned source code. It is
fetched into `third_party/limine/` by `make setup`, verified by SHA-256, and
excluded from git. See `third_party/README.md` and ADR-0004.
