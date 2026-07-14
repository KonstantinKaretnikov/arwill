# Arwill

Arwill `0.19.0` is a small experimental x86-64 operating system for QEMU.
It is built around explicit, replaceable components and documented decisions.
See [MANIFESTO.md](MANIFESTO.md).

Arwill is not a production OS.

## Current system

- Limine boot on QEMU x86-64.
- Serial owner shell with framebuffer output mirroring.
- ATA PIO storage and mutable ARFS v2.
- Small kernel heap, device registry, IDT/PIC/PIT, and monotonic uptime.
- e1000, fixed IPv4, ARP/ICMP, and a bounded TCP remote console.
- Four-slot ring 3 AWP runtime with PIT preemption and fault containment.
- Fixed-slot cooperative kernel tasks with saved x86-64 contexts and dedicated
  8 KiB stacks.
- Long-lived `network-poll` and `remote-console` system tasks, distinct from
  manually launched kernel built-ins and scheduled AWP programs.
- Persistent owner configuration, a volatile event log, and one built-in
  service.

Arwill has one owner and no accounts, groups, roles, or multi-user permission
model. Ring 3 protects the kernel from ordinary programs; it is not an account
boundary.

## Build and run

Host: macOS with Homebrew.

```sh
brew install llvm lld xorriso qemu
make setup
make build
make run
```

`make setup` fetches the pinned Limine dependency. `make run` attaches the
QEMU serial console to the terminal. Press `Ctrl+C` in the host terminal to
stop QEMU.

The boot screen is intentionally short:

```text
   A    RRR   W   W  III  L     L
  A A   R  R  W   W   I   L     L
 AAAAA  RRR   W W W   I   L     L
 A   A  R R   WW WW   I   L     L
 A   A  R  R  W   W  III  LLLL  LLLL

Arwill 0.19.0 ready
config: /owner/arwill.conf
help: type 'help' or press Tab
Arwill:/>
```

Detailed state is grouped under `system`, `devices`, and `network`. `top`
provides a live system and process dashboard.

## Shell

`help` is the authoritative command list.

| Area | Commands |
| --- | --- |
| Core | `help version system top clear exit halt` |
| Files | `pwd cd ls cat stat mkdir rm` |
| Platform | `devices` |
| Network | `network` |
| Operations | `config logs service` |
| Programs | `ps run exec` |

`Tab` completes commands, subsystem arguments, paths, and process names.
Up/Down browse in-memory history. `Ctrl+C` cancels the current line, `top`, or
a foreground AWP. Russian-layout input is normalized to ASCII key positions
for command entry only.

Inspection commands use one canonical interface per subsystem:

```text
system [memory|storage|interrupts|scheduler|runtime|owner]
devices [pci|disk0|net0]
network [ping|tcp]
top
```

Without an argument, `system` and `network` print a summary and `devices` lists
the registry. `top` refreshes once per second, keeps kernel built-ins and AWP
tasks distinct through `KIND`, and exits with `q` or `Ctrl+C`. `ps` remains the
stable one-shot process listing.

`system storage` reports the mounted filesystem's entry usage, data-sector
usage, largest free contiguous run, manifest size, and current path and file
limits. These are ARFS allocation-unit counters, not quotas or a generic disk
capacity API.

On serial, `exit` powers off QEMU. In a TCP session it closes only that
session.

## Filesystem

ARFS v2 supports directories, whole-file reads and writes, and removal. The
user-facing mutation commands manage directories and removal:

```text
mkdir [path]
rm [path]
```

Paths may be absolute or relative to the session's current directory.
Use `/apps/edit.awp` for interactive ASCII text creation and editing. Mutations
persist in the QEMU raw test disk.

Limits: 24 entries, short paths, 8192 bytes per file, and contiguous
allocation. ARFS has no append, rename, journal, atomic metadata update, or
crash consistency.

## Programs and multitasking

`exec` runs an AWP image stored in ARFS. Programs in `/apps` have a short
canonical launch form:

```text
exec hello
exec calc
exec edit /owner/arwill.conf
```

For a bare program name without `.awp`, `exec` resolves exactly
`/apps/<name>.awp`. Explicit image paths such as `exec /apps/calc.awp` and
`exec ./tool.awp` remain available. It accepts one optional file path, resolved
from the launching shell directory. `Tab` completes short program names or an
explicit image path in the first position, and a filesystem path in the second.
There are no `PATH` searches, aliases, quotes, expansion, general argument
vectors, or environment.

Included applications:

- `calc.awp`: interactive integer `+`, `-`, `*`, and `/`; type
  `exit` to leave.
- `edit.awp`: bounded 2048-byte ASCII editor. A file argument is required.
  Use arrows, Home/End, Enter, Backspace, and Delete; `Ctrl+S` saves,
  `Ctrl+Q` exits.

The runtime has four fixed AWP slots, but each shell session may own only one
foreground AWP. Arwill currently exposes two interactive sessions: serial and
one TCP session. Therefore the practical limit is **two simultaneous
interactive applications**, one in each session. The other two slots do not
provide extra user-visible concurrency because Arwill has no `jobs`, `bg`,
`fg`, or additional TCP sessions.

`run counter` exercises the stackful cooperative kernel-task path. Its local
variables and call stack survive each explicit yield, and execution resumes at
the instruction after that yield. Kernel tasks share the kernel address space
and are not preempted.
`run userhello` and `run userbad` exercise the narrow ring 3 syscall path.
These are distinct from scheduled AWP tasks.

`ps` and `top` expose `system`, `kernel`, and `awp` as separate process kinds.
The two system tasks are started at boot and cooperatively yield after each
bounded polling/service pass.

AWP is not ELF. Arwill has no general executable loader, dynamic linker,
per-process heap, `fork`, POSIX signals, or SMP.

## Remote console

`make run` forwards host `127.0.0.1:23232` to guest port `23232`.
Connect from another terminal:

```sh
old=$(stty -g); stty raw -echo; nc 127.0.0.1 23232; stty "$old"
```

Raw terminal mode delivers arrows, `Tab`, and `Ctrl+C` immediately. At
`Access key:`, enter the seeded development key `arwill`; it is not echoed.

The serial shell remains usable while the TCP shell is connected. The TCP
service accepts only one client at a time; after `exit`, another client may
connect.

Choose another host port with:

```sh
make run QEMU_REMOTE_CONSOLE_HOST_PORT=23233
```

Allow trusted-LAN connections explicitly with:

```sh
make run QEMU_REMOTE_CONSOLE_BIND=0.0.0.0
```

Then connect to the Mac's LAN address instead of `127.0.0.1`.
`QEMU_REMOTE_CONSOLE_GUEST_PORT` must match the persisted `remote.port`.

The access key is only a gate. The key and all traffic are plaintext. Do not
expose this port to the Internet; use a host-side SSH tunnel when
confidentiality is required.

## Configuration, services, and logs

Configuration is stored in `/owner/arwill.conf`. Accepted keys:
`config.version`, `remote.enabled`, `remote.port`, `remote.key`, and
`log.level`.

```text
config
config remote.port 23232
config remote.enabled true
config remote.key

service status
service start remote-console
service stop remote-console
service restart remote-console

logs
```

`config` masks the key. `config remote.key` reads a replacement without
echo. Restart the service after changing its port or key. `logs` prints the
complete 64-entry in-memory event ring; it is cleared on reboot and never
records keys, commands, or file contents.

## Important limits

- The clock is PIT-backed uptime with 10 ms resolution, not calendar time.
- The framebuffer console mirrors serial output; it is not a terminal or GUI.
- Networking has no DHCP, socket API, general routing, congestion control, SSH,
  or TLS.
- TCP keeps bounded state for the single remote-console connection, including
  one output segment for limited retransmission.

## Verification

Run all host checks and the QEMU serial/TCP smoke test:

```sh
make check
```

## Repository

- `include/`: public Arwill contracts.
- `kernel/`: architecture-independent kernel code.
- `arch/x86_64/`: x86-64 CPU and boot integration.
- `platform/qemu/`: QEMU-specific devices and wiring.
- `apps/`: AWP application sources.
- `scripts/`: setup, image creation, and verification.
- `docs/architecture/`: current architecture.
- `docs/decisions/`: ADRs.
- `docs/roadmap.md`: milestone order.

Limine is pinned external boot infrastructure, fetched into ignored
`third_party/limine/`. See [third_party/README.md](third_party/README.md) and
[ADR-0004](docs/decisions/ADR-0004-selected-boot-strategy.md).
