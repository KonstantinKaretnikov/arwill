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

Status: `0.11.0`.

Arwill already has:

- [x] architecture-first project rules, ADRs, and a pinned Limine boot path;
- [x] x86-64 plus QEMU as the first target;
- [x] freestanding C with minimal x86-64 inline assembly for port I/O and CPU
  idle;
- [x] a buildable bootable ISO and `make check` with a bounded QEMU serial smoke
  test;
- [x] QEMU serial console output and blocking serial input;
- [x] a serial shell with canonical commands only: `help`, `version`, `pwd`,
  `cd`, `clear`, `ls`, `cat`, `write`, `stat`, `meminfo`, `blkinfo`,
  `heaptest`, `irqinfo`, `irqprobe`, `schedinfo`, `userinfo`, `ownerinfo`,
  `ps`, `run`, `step`, `exit`, and `halt`;
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
- [x] framebuffer text console mirroring serial output when Limine provides a
  32-bit framebuffer;
- [x] simple Arwill Program loader through `exec [path]`;
- [x] QEMU debug-exit poweroff through `exit`;
- [x] cooperative kernel-managed processes with PID, state, run count, exit
  code, `run [name]`, cooperative `step`, and `ps`;
- [x] x86-64 IDT setup, legacy PIC remap, PIT timer interrupts, and a safe
  breakpoint exception diagnostic;
- [x] scheduler tick accounting exposed through `schedinfo`;
- [x] minimal x86-64 ring 3 user-mode entry with GDT, TSS, HHDM-backed user
  mappings, `int 0x80` syscalls for `write` and `exit`, and process-table exit
  status for built-in user programs.
- [x] single-owner OS model: one owner, no accounts or multi-user permission
  system, with the kernel/user boundary kept as an engineering guardrail.

Arwill does not yet have general filesystem allocation, arbitrary file create,
append, delete, rename, saved CPU contexts, preemptive context switching,
per-process address spaces, ELF program loading, dynamic linking, multi-user
accounts, or a general-purpose writable storage subsystem.

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

   Verified by: QEMU smoke test for `run hello`, process output, `ps`, and
   successful `exit` poweroff.

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
   - expose the model through startup output and `ownerinfo`;
   - record the decision as an ADR.

   Verified by:

   - smoke test observes `owner: single-owner`;
   - smoke test runs `ownerinfo`;
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
   - smoke test lists `/apps`;
   - smoke test verifies `cat /apps/hello.awp` remains binary-only;
   - smoke test runs `exec /apps/hello.awp`;
   - the stored program writes `awp hello from storage` through syscall `write`
     and exits with code `9`.

   Definition of done: Arwill can load and run a tiny stored program image
   through the current syscall boundary.
