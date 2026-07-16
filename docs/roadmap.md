# Arwill Roadmap

This roadmap records the current agreed order for large operating-system
milestones. It is intentionally sequential: finish and verify one layer before
depending on it from the next layer.

## Working Rule

Prefer more tests and smaller verified steps over broad subsystem jumps. Every
milestone must keep Arwill bootable, update documentation, and extend the
bounded QEMU smoke test or add another suitable automated check.

Do not turn a future subsystem into a placeholder. If a layer cannot yet do real
work, keep it absent or label the limitation explicitly.

## Completed Baseline

Status: `0.21.1`.

Arwill already has:

- [x] architecture-first project rules, ADRs, and a pinned Limine boot path;
- [x] x86-64 plus QEMU as the first target;
- [x] freestanding C with minimal x86-64 inline assembly for port I/O and CPU
  idle;
- [x] a buildable bootable ISO and `make check` with native IPv4/TCP checks plus
  a bounded QEMU serial/TCP smoke test;
- [x] one bootable `arwill.img` system disk containing the Limine boot image
  and a bounded persistent ARFS region, used by normal QEMU run and smoke paths;
- [x] QEMU serial console output and blocking serial input;
- [x] a serial shell with canonical commands only: `help`, `version`, `system`,
  `devices`, `network`, `top`, `pwd`, `cd`, `clear`, `ls`, `cat`, `mkdir`,
  `rm`, `stat`, `config`, `logs`, `service`, `ps`, `run`, `exec`, `exit`, and
  `halt`; exact internal smoke and transitional diagnostic commands are kept
  outside `help` and Tab completion;
- [x] shell current directory state, path resolution, Tab completion, command
  history, and Russian-layout command-entry normalization;
- [x] a static read-only boot catalog used by `ls`, `cd`, `cat`, `stat`, and
  path completion before disk mount;
- [x] read-only file contents for selected text files in that static catalog;
- [x] read-only sector access from a QEMU-attached raw test disk through an ATA
  PIO block-device contract;
- [x] storage-backed ARFS mounted from the raw test disk for `ls`, `cd`, `cat`,
  `stat`, and path completion;
- [x] single-sector ATA PIO block writes and a persistent ARFS owner-note
  overwrite path at `/owner/note`;
- [x] a Limine memory map snapshot, first bump-only physical page allocator
  counters, and a small HHDM-backed kernel heap;
- [x] a tiny fixed-size device registry and `devices` shell command;
- [x] minimal identical boot text on serial and framebuffer when Limine
  provides a 32-bit framebuffer;
- [x] simple Arwill Program loader through `exec [program] [file]`, with short
  `/apps` names, explicit image paths, and at most
  one bounded, shell-resolved launch file path;
- [x] QEMU debug-exit poweroff through `exit`;
- [x] stackful cooperative kernel-managed tasks with PID, state, run count,
  exit code, saved x86-64 contexts, fixed 8 KiB stacks, `run [name]`, internal
  `step`, and `ps`;
- [x] boot-created `network-poll` and `remote-console` system tasks with an
  automatic cooperative dispatch pass and explicit `system` process kind;
- [x] x86-64 IDT setup, legacy PIC remap, PIT timer interrupts, and a safe
  breakpoint exception diagnostic;
- [x] scheduler tick accounting exposed through `system scheduler`;
- [x] minimal x86-64 ring 3 user-mode entry with GDT, TSS, HHDM-backed user
  mappings, `int 0x80` syscalls for `write`, `exit`, `read`, and `clock`, and
  process-table exit status for built-in user programs;
- [x] a PIT-backed monotonic clock exposed through `system` and syscall `4`;
- [x] bounded PCI discovery and `devices pci`, with a QEMU e1000 device attached
  to the bounded network path;
- [x] a plaintext single-connection TCP remote console for `nc`, forwarded
  only through host localhost by default;
- [x] a fixed four-slot preemptive AWP runtime with saved ring 3 contexts,
  per-slot address spaces, session input, and user-fault containment;
- [x] bounded AWP whole-text-file syscalls and `/apps/edit.awp`;
- [x] persistent owner configuration, a 64-entry event ring, and the minimal
  `config`, `logs`, and `service` command surface;
- [x] a three-attempt remote access-key gate plus explicit trusted-LAN QEMU
  bind override;
- [x] grouped `system`, `devices`, and `network` inspection interfaces with
  fixed Tab-completed subsystem arguments;
- [x] a per-session live `top` dashboard with one-second refresh, explicit
  kernel/AWP process kinds, and `q`/Ctrl+C exit;
- [x] bounded live filesystem allocation statistics through `system storage`;
- [x] single-owner OS model: one owner, no accounts or multi-user permission
  system, with the kernel/user boundary kept as an engineering guardrail.

Arwill has an ARFS v2 mutable core exposed to users through directory creation,
interactive ASCII editing, and removal. Internal smoke commands retain bounded
whole-file text and binary writes. Arwill does not have append, rename,
ELF program loading, dynamic linking, kernel stack guards, kernel preemption,
SMP, multi-user accounts, or a general-purpose writable storage
subsystem.

## Product Direction

Arwill is intentionally a single-owner operating system. The owner should be
able to control the whole machine directly. Arwill should not grow Unix-style
multi-user accounts, groups, or role permissions unless a future ADR explicitly
changes that direction.

The kernel/user boundary still matters. It is not meant to restrict the owner;
it is meant to keep ordinary programs from accidentally corrupting the kernel,
storage, or hardware state. Privileged access should be explicit kernel or
driver work, not accidental default access for every ring 3 program.

## Milestones

1. [x] Kernel cooperative processes

   Status: done in `0.1.0`.

   Arwill can launch built-in cooperative kernel processes with `run [name]`
   and inspect them with `ps`. These began as kernel-managed run-to-completion
   work units, not user-space processes.

   Historical verification used `run hello`; that redundant built-in was
   retired in `0.17.3` per ADR-0053. Current smoke retains `counter`, `ps`, and
   successful `exit` poweroff coverage.

2. [x] Block device reads

   Status: done in `0.2.0`.

   Goal: read real sectors from a QEMU-provided disk image through an explicit
   block-device contract.

   Scope:

   - add a narrow read-only block-device contract;
   - add a deterministic host-side test disk image fixture;
   - attach that image in the QEMU run and smoke paths;
   - implement the first QEMU-backed sector read path;
   - expose only enough diagnostics to prove the contract, such as block device
     identity and one deterministic sector read.

   Candidate first driver: QEMU IDE/ATA PIO, because Arwill already has x86-64
   port I/O. Revisit before implementation if virtio-blk becomes the cleaner
   first target.

   Verified by:

   - `make check` still boots and powers off;
   - smoke test observes `block: qemu ata pio`;
   - smoke test runs `blkinfo`;
   - smoke test verifies `sample: ARWILL-BLOCK-DEVICE-TEST`, read from LBA 1 of
     the deterministic raw test image.

   Definition of done: Arwill can read a known sector from a real QEMU-attached
   image without any filesystem parser involved.

3. [x] Real read-only filesystem

   Status: done in `0.3.0`.

   Goal: replace the static boot catalog path with a filesystem implementation
   backed by block storage.

   Scope:

   - keep the existing `filesystem` contract or evolve it deliberately with an
     ADR;
   - choose one first read-only on-disk format;
   - route `ls`, `cd`, `cat`, `stat`, and path completion through the
     storage-backed implementation;
   - keep the static catalog only as a fallback or remove it once the real path
     is stable.

   Candidate formats: ISO9660 for reading the boot image, a simple initrd/tar
   image for a smaller parser, or another deliberately chosen read-only format.
   Pick the format at the start of this milestone and document why.

   Verified by:

   - smoke test observes `filesystem: arfs read-only disk`;
   - smoke test lists `/`, `/boot`, and `/boot/limine` through ARFS;
   - smoke test reads `/boot/limine/limine.conf`, `/system/identity`, and
     `/docs/readme` through `cat`;
   - smoke test checks `stat /system/identity`;
   - smoke test covers binary-file handling with `/boot/kernel.elf`;
   - smoke test covers missing-file handling with `/docs/missing`.

   Definition of done: the shell's filesystem commands no longer depend on
   hard-coded directory entries for the primary happy path.

4. [x] Interrupts, timer, and scheduler foundation

   Status: done in `0.4.0`.

   Goal: create the interrupt and timer foundation needed to move beyond
   shell-triggered cooperative kernel processes in later milestones.

   Scope:

   - add IDT setup and a safe breakpoint exception diagnostic;
   - remap the legacy PIC and unmask IRQ0 only;
   - add the PIT as the first timer source;
   - route timer IRQs into a scheduler tick function;
   - expose interrupt and scheduler diagnostics through shell commands;
   - keep behavior deterministic enough for smoke tests;
   - explicitly defer saved execution contexts and preemptive context switching.

   Verified by:

   - smoke test observes interrupt and timer initialization;
   - smoke test runs `irqinfo` and observes a timer tick;
   - smoke test runs `irqprobe` and observes handled breakpoint vector 3;
   - smoke test runs `schedinfo` and observes scheduler tick accounting for the
     `shell` and `idle` slots;
   - tests avoid exact tick counts and do not depend on fragile host timing.

   Definition of done: Arwill has an observable IDT/PIC/PIT path and a timer
   callback into scheduler accounting, ready for a later saved-context
   scheduler.

5. [x] User-space v1

   Status: done in `0.5.0`.

   Goal: run the first isolated user-mode program.

   Scope:

   - add the required descriptor/user-mode entry groundwork;
   - choose deliberately generated built-in machine-code programs as the first
     executable format;
   - request Limine HHDM and map user code and stack pages with user access;
   - add a minimal `int 0x80` syscall ABI with `write` and `exit`;
   - connect process exit status to the process table;
   - keep the limitation explicit: no ELF loader or per-process address spaces
     yet.

   Verified by:

   - smoke test launches `run userhello`;
   - user output reaches the serial console through syscall `write`;
   - syscall `exit` returns code `7`, visible through `ps`;
   - smoke test launches `run userbad`;
   - an unknown syscall exits with code `127`, increments the bad-syscall
     counter, and does not crash the kernel;
   - `userinfo` reports HHDM, GDT, TSS, syscall gate, and syscall counters.

   Definition of done: Arwill can run a separate ring 3 user-mode program with
   a clear first kernel/user syscall boundary.

5a. [x] Single-owner OS model

   Status: done in `0.5.1`.

   Goal: make the ownership model explicit before adding more user-mode and
   storage behavior.

   Scope:

   - document that Arwill has one owner and no account system;
   - keep the owner fully in control of the machine;
   - keep ring 3 as a safety boundary for ordinary programs, not as a
     multi-user permissions mechanism;
   - expose the model through `ownerinfo` and system documentation;
   - record the decision as an ADR.

   Verified by:

   - smoke test runs `ownerinfo` and observes the single-owner model;
   - docs and AGENTS distinguish CPU user mode from OS user accounts.

6. [x] Writable filesystem

   Status: done in `0.6.0` as a narrow first writable slice.

   Goal: add controlled persistent write support after read storage, filesystem
   reads, scheduling direction, and user-space basics are established.

   Scope:

   - add block write support with error reporting;
   - evolve the filesystem contract with a narrow whole-file overwrite
     operation;
   - reserve `/owner/note` as the first writable ARFS text file;
   - store the note data and note size in explicit reserved disk sectors;
   - keep arbitrary create, append, allocation, delete, rename, and crash
     consistency out of scope.

   Verified by:

   - smoke test runs `write /owner/note ...`;
   - smoke test reads the note back in the same boot;
   - smoke test powers off, boots QEMU again with the same raw disk image, and
     reads the note back again;
   - keep read-only filesystem tests passing.

   Definition of done: Arwill can persist a small file change to a QEMU disk
   image and observe it again after reboot.

7. [x] Cooperative yield and saved process progress

   Status: done in `0.7.0` as an explicit process-result contract.

   Goal: let simple built-in processes pause and continue without introducing
   preemptive scheduling or CPU context switching.

   Scope:

   - change process entries to return either `finished` or `yielded`;
   - keep yielded processes in the ready state;
   - expose `step` as the shell command that runs one ready cooperative step;
   - use `run_count` as the first saved progress value for tiny built-ins;
   - keep saved CPU register contexts, separate kernel stacks, preemption, and
     user-space scheduling out of scope.

   Verified by:

   - smoke test launches `run counter`;
   - `counter` prints step `1/3` and remains ready;
   - smoke test runs `step` twice and observes steps `2/3` and `3/3`;
   - `ps` shows the yielded process as ready and later finished.

   Definition of done: a cooperative kernel process can yield, remain visible
   as ready, and continue later from explicit saved progress.

8. [x] Kernel heap

   Status: done in `0.8.0` as a small HHDM-backed free-list allocator.

   Goal: add a small, explainable kernel allocator for dynamic kernel objects
   without making memory management clever too early.

   Scope:

   - build on the existing physical page allocator and Limine HHDM;
   - reserve a small contiguous heap during boot;
   - provide a narrow `kmalloc`/`kfree` contract;
   - split and coalesce free-list blocks;
   - expose allocator diagnostics through `meminfo`;
   - add `heaptest` to allocate and free small blocks in the smoke path;
   - keep paging replacement, swapping, userspace heap, and general VM policy
     out of scope.

   Verified by:

   - startup output reports `allocator: physical page bump allocator + kernel
     heap`;
   - smoke test runs `meminfo` and observes kernel heap counters;
   - smoke test runs `heaptest` and observes two allocations and frees.

   Definition of done: kernel code can allocate and release small dynamic
   objects with bounded diagnostics and smoke-test coverage.

9. [x] Device registry

   Status: done in `0.9.0` as a fixed-size inspection table.

   Goal: make detected platform devices visible through a tiny registry.

   Scope:

   - register current devices such as serial console, block device, timer,
     poweroff, and user runtime where appropriate;
   - add a `devices` shell command;
   - keep this as inspection and explicit handles, not a large driver model.

   Verified by:

   - startup output reports `devices: registry`;
   - smoke test runs `devices`;
   - smoke test observes serial, input, disk, filesystem, heap, timer, power,
     and user runtime entries.

   Definition of done: Arwill can list its current devices and their basic
   status without creating cross-layer shortcuts.

10. [x] Framebuffer text console

   Status: done in `0.10.0` as a serial mirror.

   Goal: add a simple screen console for board-style experimentation while
   keeping serial as the primary test channel.

   Scope:

   - use Limine framebuffer information on x86-64/QEMU;
   - request a framebuffer from Limine;
   - draw basic text output with a built-in bitmap font;
   - mirror the serial console so existing boot and shell output appears on
     screen too;
   - keep graphics, windows, fonts beyond one built-in bitmap font, and input
     focus out of scope.

   Verified by:

   - smoke test still uses serial output as the authoritative channel;
   - device registry lists `fb0` as a ready Limine framebuffer text console;
   - existing shell smoke coverage continues through the mirror console.

   Definition of done: boot status and shell output can be mirrored to a basic
   framebuffer text console while serial smoke tests remain authoritative.

11. [x] Simple program image loader

   Status: done in `0.11.0`, then renamed to Arwill Program v1.

   Goal: run owner-provided programs from storage without jumping straight to
   full ELF/POSIX complexity.

   Scope:

   - define a small Arwill Program format with a header and code bytes;
   - load a program image from ARFS;
   - run it through the existing ring 3 syscall ABI;
   - keep ELF, dynamic linking, files-as-processes, arguments, environment,
     and per-process address spaces out of scope until explicitly needed.

   Verified by:

   - test disk includes `/apps/hello.awp`;
   - `apps/hello/` builds the application separately from the disk image;
   - smoke test lists `/apps`;
   - smoke test verifies `cat /apps/hello.awp` remains binary-only;
   - smoke test runs `exec /apps/hello.awp`;
   - the stored program writes `awp hello from storage` through syscall `write`
     and exits with code `9`.

   Definition of done: Arwill can load and run a tiny stored program image
   through the current syscall boundary.

12. [x] ARFS v2 mutable core

   Status: implemented after `0.11.0` as a bounded internal filesystem layer.

   Scope: persist a fixed mutable manifest, create directories, write complete
   text or binary files, remove entries, and infer reusable contiguous space
   from the manifest. Shell exposure was intentionally deferred to the next
   milestone.

   Verified by: clean build and the existing persistent owner-note smoke path,
   which now persists its changed size through the ARFS v2 manifest.

13. [x] ARFS v2 shell mutations

   Status: done in `0.12.0`.

   Scope: expose the existing mutable filesystem contract through canonical
   `mkdir`, `write`, `writehex`, and `rm` commands. Text and binary files use
   whole-file writes; directories must be empty before removal.

   Verified by: the QEMU smoke test creates a directory plus text and binary
   files, reboots, verifies their persisted type, size, and contents, executes
   the stored AWP application, removes the entries, and verifies that the first
   released data sector is reused.

14. [x] Interactive AWP calculator

   Status: implemented in the current `0.12.0` increment.

   Scope: add the minimal serial-input syscall and package a plain integer
   calculator under `/apps/calc.awp`. This does not add a general input API,
   process scheduling, or a language runtime.

   Verified by: smoke execution of `12*7`, observing `84` and a clean exit.

15. [x] Monotonic timekeeping v1

   Status: done in `0.13.0`.

   Scope: add an architecture-independent clock contract backed by the 100 Hz
   x86-64 PIT counter, expose monotonic milliseconds through `uptime`, and add
   syscall `4` (`clock`) for AWP programs.

   Verified by: the QEMU smoke test observes two increasing `uptime` values,
   including one after ring 3 and stored AWP execution, and runs a temporary AWP
   syscall probe. RTC/CMOS calendar time, dates, timezones, NTP, and process
   timers remain out of scope.

16. [x] PCI discovery v1

   Status: done in `0.14.0`.

   Scope: scan bus 0 through PCI configuration mechanism #1, expose fixed
   vendor/device/class/BAR diagnostics through `pciinfo`, and attach a real
   QEMU e1000 device for future driver work.

   Verified by: smoke discovery of Intel vendor `8086`, device `100e`.

17. [x] QEMU Ethernet driver

   Status: done in `0.14.0`.

   Scope: bind the discovered e1000, map its MMIO BAR, enable PCI bus mastering,
   and implement bounded frame TX/RX through a replaceable network-device
   contract. `netinfo` reports the deterministic MAC and `netprobe` sends a
   minimum-size broadcast frame.

   Verified by: QEMU smoke transmission of a 60-byte Ethernet diagnostic
   frame. Interrupt-driven networking, sockets, and IP configuration remain
   out of scope for this milestone.

18. [x] ARP/IPv4/ICMP foundation

   Status: done in the current `0.14.0` increment.

   Scope: fixed QEMU configuration (`10.0.2.15/24` and gateway `10.0.2.2`),
   ARP reply handling, and one bounded ICMP echo request/reply through `ping`.
   DHCP, routing, and sockets remain out of scope.

   Verified by: QEMU smoke observes `ping: reply received`.

19. [x] TCP remote console

   Status: done in `0.15.0` per ADR-0041.

   Scope: retain the bounded e1000/ARP/IPv4/TCP path, listen on guest port
   `2323`, and expose an interactive plaintext shell to `nc` through a
   localhost-only QEMU forward. Serial and TCP sessions have independent input
   state and share one canonical command dispatcher. Remote `exit` closes the
   connection without powering off Arwill.

   Verified by: the QEMU smoke test opens two sequential real `nc`
   connections, checks command execution, Backspace, Ctrl+C, remote `exit`,
   and listener reuse.

20. [x] Bounded TCP robustness

   Status: done in `0.15.1` per ADR-0042.

   Scope: validate inbound IPv4 and TCP checksums, re-ACK duplicate or
   out-of-order data, retain one unacknowledged SYN-ACK or console-output
   segment, retry it three times at a fixed 250 ms interval, and reset a timed
   out listener. Keep the current console as a single-connection polling
   service; do not introduce a socket API, congestion control, adaptive timers,
   or a full close-state machine.

   Verified by: a native host test with fake network and clock devices plus the
   existing real QEMU/`nc` smoke path.

21. [x] User multitasking v1

   Status: implemented for `0.16.0` per ADR-0043.

   Scope: four fixed AWP slots, saved ring 3 contexts, per-slot address spaces,
   PIT-only user preemption, round-robin dispatch, session-bound blocking input,
   Ctrl+C/disconnect cancellation, and user-fault containment. Cooperative
   kernel built-ins remain a separate process kind.

22. [x] AWP file I/O and editor

   Status: implemented for `0.16.0` per ADR-0044.

   Scope: bounded whole-text-file read/write syscalls and `/apps/edit.awp`.

23. [x] Configuration and event log v1

   Status: implemented for `0.16.0` per ADR-0045.

   Scope: `/owner/arwill.conf`, one `config` command, a 64-entry in-memory event
   ring, and one `logs` command.

24. [x] Authenticated remote-console service

   Status: implemented for `0.16.0` per ADR-0046.

   Scope: config-selected port, key gate, explicit LAN host-forward override,
   and `service` status/start/stop/restart. HTTP and HTTPS remain outside the
   active roadmap.

25. [x] Single AWP launch file path

   Status: implemented in `0.16.0` per ADR-0048.

   Scope: one optional 63-byte file path through `exec`, Tab completion in both
   positions, shell-current-directory resolution, bounded syscall `7`
   retrieval, and mandatory file-path launch for `/apps/edit.awp`. No
   `argc`/`argv`, quoting, environment, or task working directory.

26. [x] Grouped inspection and live top

   Status: implemented in `0.17.0` per ADR-0050.

   Scope: group current inspection behavior under `system`, `devices`, and
   `network`; complete their fixed subsystem arguments; and add a nonblocking
   per-session `top` dashboard with one-second refresh and explicit kernel/AWP
   process kinds. Keep `ps` as the stable one-shot process listing.

   Verified by: QEMU smoke exercises each grouped interface, observes two live
   dashboard renders, exits with `q`, and confirms retired names are absent
   from `help`.

27. [x] Filesystem storage statistics

   Status: implemented in `0.17.1` per ADR-0051.

   Scope: add one optional architecture-independent filesystem statistics
   snapshot and expose ARFS entry use, data-sector use, largest contiguous free
   run, manifest size, and current path/file limits through `system storage`.
   Do not generalize this into quotas, partitions, or a disk-capacity model.

   Verified by: QEMU smoke observes the initial 15/24 entry use, 18/24 after
   bounded mutations, persistence across reboot, and return to 15/24 after
   cleanup.

28. [x] Short AWP program launch names

   Status: implemented in `0.17.2` per ADR-0052.

   Scope: resolve a bare `exec` program name without `.awp` exactly to
   `/apps/<name>.awp`, complete first-position program names without their
   extension, and preserve explicit image paths and the one launch-file path.
   Do not add `PATH`, aliases, multiple search directories, or arguments.

   Verified by: QEMU smoke launches `hello`, completes `ca` to `calc`, opens a
   relative file through `edit`, exercises the same form over TCP, and retains
   an explicit `/apps/hello.awp` launch after reboot.

29. [x] Retire the kernel hello built-in

   Status: implemented in `0.17.3` per ADR-0053.

   Scope: remove the run-to-completion `run hello` demonstration from code,
   process-name completion, and current documentation. Retain `counter` as the
   cooperative kernel-process probe and keep ring 3 `userhello` plus the stored
   `/apps/hello.awp` fixture as distinct mechanisms.

   Verified by: QEMU smoke rejects `run hello`, lists only `counter`,
   `userhello`, and `userbad`, and exercises the remaining process kinds.

30. [x] Kernel task contexts v1

   Status: implemented in `0.18.0` per ADR-0054.

   Scope: give every fixed kernel-task slot a dedicated 8 KiB stack, inject an
   architecture context backend into the process manager, save the x86-64
   stack pointer and callee-saved registers at explicit yield boundaries, and
   resume at the instruction after `arwill_process_yield`. Keep the shared
   kernel address space, cooperative dispatch, fixed task table, and existing
   `run`, `ps`, and internal `step` interfaces.

   Verified by: QEMU smoke observes `counter` yield as ready after its first
   dispatch, then resume twice with a stack-local value progressing from 10 to
   11 to 13 before finishing with three runs.

31. [x] System tasks v1

   Status: implemented in `0.19.0` per ADR-0055.

   Scope: create long-lived `network-poll` and `remote-console` tasks at shell
   startup; automatically dispatch only the `system` kind from the main loop;
   retain manual `kernel` built-in stepping; and show both kinds separately in
   `system`, `ps`, and `top`. Use scoped scheduler contexts so a remote system
   task may safely dispatch a nested kernel built-in.

   Verified by: the full QEMU TCP smoke retains authentication, service
   lifecycle, retransmission-safe output, concurrent AWP operation, and remote
   shell behavior. It also runs and completes `counter` from the remote console
   before continuing the same connection through `system`, `top`, and `exit`.

32. [x] Bootable system disk v1

   Status: implemented in `0.20.0` per ADR-0056.

   Scope: produce one `build/arwill.img` containing the existing hybrid Limine
   boot image and a fixed 1 MiB ARFS region at LBA 32768; add an
   architecture-independent bounded block-device region; boot QEMU from that
   one IDE disk; and keep smoke mutations on a disposable copy. Do not add a
   partition parser, interactive installer, disk selection, or filesystem
   formatter inside the kernel.

   Verified by: host tests cover region translation and bounds, artifact
   checks locate the ARFS2 superblock at the configured offset, and the full
   two-boot QEMU smoke test boots without a CD-ROM or second disk and verifies
   persistent text and binary writes across reboot. A focused smoke also boots
   the image from the secondary-master IDE slot and verifies ARFS mount for UTM
   compatibility.

33. [x] Framebuffer boot presentation

   Status: implemented in `0.20.1` per ADR-0057.

   Scope: add one optional semantic boot-banner operation to the console
   contract; retain a serial ASCII fallback; and let the Limine framebuffer
   render a fixed, scaled, colored Arwill splash before returning to normal
   text mirroring. Do not add image assets, a bitmap loader, a graphics API,
   themes, or animation.

   Verified by: the full QEMU smoke confirms the serial fallback and versioned
   ready line, while a QEMU framebuffer screendump is rendered for visual QA.

   Superseded in `0.20.2` by ADR-0058 after deciding that the additional visual
   identity did not fit Arwill's minimal system surface.

34. [x] Minimal boot identity

   Status: implemented in `0.20.2` per ADR-0058.

   Scope: remove the specialized boot-presentation console operation, scaled
   framebuffer drawing, logo, tagline, and presentation-only test. Emit only
   `Arwill <version> ready` through the normal text console before the prompt.

   Verified by: the full QEMU smoke requires the single ready line across both
   boots and rejects the retired logo, tagline, and startup hints.

35. [x] Host-visible ICMP echo replies

   Status: implemented in `0.20.3` per ADR-0059.

   Scope: extend the existing bounded network poll path to validate and answer
   unfragmented ICMP echo requests addressed to Arwill while retaining the
   fixed `10.0.2.15/24` configuration. Document a matching UTM macOS Shared
   network so the host can reach the guest directly. Do not add DHCP, dynamic
   addressing, IP fragmentation, or a general ICMP API.

   Verified by: the native IPv4 host test checks a valid echo reply including
   checksums, identifier, sequence, and payload, and rejects a request with a
   bad ICMP checksum. UTM remains a manual platform target.

36. [x] Bounded TCP lifecycle v2

   Status: implemented in `0.21.0` per ADR-0060.

   Scope: separate transport and remote-console counters, retain the complete
   connection tuple, use wrap-safe sequence comparisons, and implement bounded
   FIN/RST close states while preserving the existing console protocol.

   Verified by: native packet tests cover closed-port RST, tuple rejection,
   sequence wraparound, active close, passive close, and retained reliability;
   the full QEMU smoke retains authenticated console reuse and clean exit.

37. [x] Bounded TCP flow control and retransmission v2

   Status: implemented in `0.21.1` per ADR-0060.

   Scope: advertise receive capacity, negotiate MSS, add adaptive bounded
   retransmission timing, and retain a fixed multi-segment send queue without
   dynamic allocation.

   Verified by: native packet tests cover MSS negotiation, three-segment
   flight with cumulative acknowledgement, bounded adaptive RTO, receive-ring
   saturation, rejection without false acknowledgement, zero-window
   advertisement, and explicit window reopening. The full QEMU TCP smoke
   retains interactive `nc` behavior.

38. [x] Kernel TCP stream contract

   Completed in `0.22.0` per ADR-0060.

   Scope: separate IPv4/TCP framing from remote-console ownership through a
   nonblocking architecture-independent kernel stream API.

   Verified by: the remote-console service and shell consume `tcp_stream`
   operations instead of IPv4 policy calls; native tests cover bounded queue
   acceptance, oversized-write rejection, and nonblocking close requests; the
   QEMU smoke retains authentication, interactive shell I/O, reconnect, and
   service restart behavior.

39. [x] Fixed TCP endpoint table

   Completed in `0.22.1` per ADR-0060.

   Scope: bind and dispatch a fixed number of simultaneous ports and
   connections by complete tuple, with per-endpoint queues and diagnostics.

   Verified by: native packet tests allocate the three application slots,
   reject table overflow and duplicate binds, establish two ports at once,
   transmit independent send flights, and retain distinct pending-ACK state.
   The QEMU smoke retains the endpoint-0 remote console and reports fixed-table
   allocation diagnostics.

40. [x] Bounded AWP networking ABI

   Completed in `0.23.0` per ADR-0060 and ADR-0061.

   Scope: give each fixed AWP slot a bounded handle table and nonblocking
   network operations, with task-owned cleanup and one stored program as the
   first real second consumer of the endpoint table.

   Verified by: native tests cover per-owner handle exhaustion, global
   endpoint exhaustion, duplicate bind, bounded I/O, retry, active open, and
   forced owner cleanup. QEMU runs `/apps/netserve.awp` on guest port 23233
   while the serial and authenticated remote shells remain active, exchanges
   payload bytes, closes the stream, and observes a clean AWP exit.

41. [ ] TCP v2 integration and fault-injection stabilization

   Planned for `0.23.1` per ADR-0060.

   Scope: complete loss, duplication, reordering, wraparound, queue-pressure,
   multi-port, invalid-user-buffer, QEMU, and UTM verification while retaining
   all established serial, filesystem, scheduler, and remote-console behavior.
