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

Version: `0.16.0`

The current milestone boots an x86-64 kernel in QEMU through Limine and starts
a serial owner shell. It has ATA PIO storage, mutable ARFS, a small kernel heap,
a device registry, PIT-backed monotonic time, and a bounded e1000/IPv4/TCP
path. Serial output is also mirrored to a framebuffer text console when one is
available.

Stored AWP programs now run in four fixed ring 3 slots with saved CPU contexts,
preallocated per-slot address spaces, PIT user preemption, session-bound input,
and user-fault containment. `/apps/calc.awp` and `/apps/edit.awp` can therefore
run at the same time in the serial and TCP sessions. AWP file access remains a
bounded whole-text-file syscall interface rather than file descriptors.

Operational state stays small: `/owner/arwill.conf` contains the remote-console
policy, `logs` prints a 64-entry volatile event ring, and `service` controls the
single built-in `remote-console` service. The TCP console asks for an access
key before exposing the canonical shell dispatcher.

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
netinfo
netprobe
netcfg
arping
ping
tcpcheck
tcplisten
tcpinfo
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
config
logs
service status
service start remote-console
service stop remote-console
service restart remote-console
ps
run [name]
exec [path]
step
exit
halt
```

`ls` lists the current filesystem. In the normal QEMU test path this is ARFS
mounted from the raw test disk image. The fixture includes `/apps`, `/boot`,
`/boot/limine`, `/docs`, `/owner`, and `/system`; bounded mutable paths can be
created as well. `cd` supports absolute paths, relative paths, `.`, and `..`.

`cat` displays text files from ARFS, such as `/system/identity`,
`/boot/limine/limine.conf`, and `/docs/readme`. Binary boot artifacts are
visible in listings, but their contents are not displayed yet.

`mkdir` creates a directory, `write` creates or replaces a complete text file,
`writehex` creates or replaces a binary file from pairs of hexadecimal digits,
and `rm` removes a file or empty directory. Mutations are persisted to the QEMU
raw disk image. ARFS v2 remains bounded to 24 entries, short paths, files of at
most 8192 bytes, and contiguous allocation; it has no append, rename, journal,
or crash consistency. The editor itself caps documents at 2048 bytes.

`stat` displays directory and file metadata. `devices` lists the current
platform devices and their basic status. `meminfo` prints the
Limine-provided boot memory map, physical page allocator counters, and kernel
heap counters. `heaptest` allocates and frees two small heap blocks.

`blkinfo` displays the detected QEMU ATA PIO block device, its sector geometry,
and a sample string read from LBA 1 of the deterministic test disk image.

`irqinfo` displays the x86-64 interrupt setup status and PIT timer tick count.
`irqprobe` triggers a safe breakpoint exception and verifies that vector 3 is
handled. `schedinfo` displays kernel/user timer accounting and the AWP
round-robin scheduler state.

`uptime` displays monotonic milliseconds since PIT initialization. The same
value is available to AWP programs through syscall `4` (`clock`). This is not
calendar time: Arwill still has no RTC/CMOS reader, date, timezone, or NTP.

`pciinfo` lists the bounded PCI scan used to discover platform devices. The
current QEMU path attaches an Intel e1000 NIC. `netinfo` reports its fixed
diagnostic MAC and `netprobe` transmits a bounded broadcast Ethernet frame.

`netcfg` shows the fixed QEMU user-network address (`10.0.2.15/24`) and
gateway (`10.0.2.2`). `arping` constructs and transmits an ARP request for
that gateway. `ping` completes one bounded ARP/ICMP echo exchange with the
QEMU gateway. The `remote-console` service listens on `remote.port` (23232 by
default) and serves the same shell command dispatcher as the serial console
after a successful `remote.key` check. It permits three attempts and never
echoes the key. The listener validates inbound IPv4 and TCP checksums, re-ACKs
duplicate data, keeps one unacknowledged segment for three fixed 250 ms retries,
and uses a bounded byte queue so AWP syscalls never wait for a TCP ACK. DHCP,
general routing, a socket API, congestion control, and multiple simultaneous
connections remain absent.

Interactive `make run` forwards host `127.0.0.1:23232` to guest TCP port
`23232`. Connect from another terminal with:

```sh
old=$(stty -g); stty raw -echo; nc 127.0.0.1 23232; stty "$old"
```

Raw mode is required for Up/Down, Tab, and Ctrl+C to reach Arwill immediately;
plain `nc` leaves the host terminal in canonical mode. The final `stty` command
restores the terminal settings after remote `exit` closes the connection.
At `Access key:` type the seeded development key `arwill` and press Enter; the
key is intentionally not echoed.

The remote console supports interactive command echo, Enter, Backspace,
Ctrl+C line cancellation, command history, completion, and `exit` to close only
the remote session. A second `nc` connection can then reuse the listener. To
choose another host port, run for example
`make run QEMU_REMOTE_CONSOLE_HOST_PORT=23233`, then connect with
`old=$(stty -g); stty raw -echo; nc 127.0.0.1 23233; stty "$old"`.

To accept connections through the host's LAN interfaces, opt in explicitly:

```sh
make run QEMU_REMOTE_CONSOLE_BIND=0.0.0.0
```

Then connect to the Mac's LAN address on port 23232. This interface is still
plaintext: the key and all shell traffic are observable on the network. It is
only an access gate for a trusted LAN, must not be exposed to the Internet, and
does not replace SSH or TLS. Use a host-side SSH tunnel when confidentiality is
required. `QEMU_REMOTE_CONSOLE_GUEST_PORT` must match the persisted
`remote.port` value if that guest setting is changed.

`config` prints the five accepted settings and masks `remote.key`.
`config remote.port <port>`, `config remote.enabled <true|false>`, and
`config log.level info` persist non-secret values. `config remote.key` reads a
replacement without echo. `remote.enabled` controls boot startup; use `service
restart remote-console` to reload the persisted port or key immediately.
`logs` prints all retained boot, config, service, connection, authentication,
AWP, fault, and file-write events. The event ring is lost on reboot.

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
the ring 3 `int 0x80` syscall boundary. AWP is deliberately small and is not
ELF, POSIX, dynamic linking, arguments, or environment support.

`exec /apps/calc.awp` runs the deliberately plain interactive calculator. Type
one expression such as `12*7` and press Enter. It supports integer `+`, `-`,
`*`, and `/`; division by zero and malformed expressions print `error`.

`exec /apps/edit.awp` opens the bounded ASCII editor. Enter a path, edit with
arrows, Enter, Backspace, Delete, Home/End, save with Ctrl+S, and leave with
Ctrl+Q. Ctrl+C exits, and Ctrl+Q asks again when unsaved changes exist. There
is no undo, selection, clipboard, search, Unicode, or syntax highlighting.

The test application build recipes live under `apps/`; their outputs are
packaged into the ARFS test disk as `.awp` files. Arwill still has no ELF
loader, dynamic linker, arguments, environment, userspace heap, fork, signals,
independent kernel stacks, kernel preemption, or SMP.

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

Run all available verification, including the deterministic host IPv4/TCP test
and the bounded QEMU serial/TCP smoke test:

```sh
make check
```

## Expected Serial Output

```text
Arwill 0.16.0
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
scheduler: AWP round-robin
user: x86_64 ring3 awp scheduler
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
