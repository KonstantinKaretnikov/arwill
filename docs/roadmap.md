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

Status: `0.1.0`.

Arwill already has:

- [x] architecture-first project rules, ADRs, and a pinned Limine boot path;
- [x] x86-64 plus QEMU as the first target;
- [x] freestanding C with minimal x86-64 inline assembly for port I/O and CPU
  idle;
- [x] a buildable bootable ISO and `make check` with a bounded QEMU serial smoke
  test;
- [x] QEMU serial console output and blocking serial input;
- [x] a serial shell with canonical commands only: `help`, `version`, `pwd`,
  `cd`, `clear`, `ls`, `cat`, `stat`, `meminfo`, `ps`, `run`, `exit`, and
  `halt`;
- [x] shell current directory state, path resolution, Tab completion, command
  history, and Russian-layout command-entry normalization;
- [x] a static read-only boot catalog used by `ls`, `cd`, `cat`, `stat`, and
  path completion;
- [x] read-only file contents for selected text files in that static catalog;
- [x] a Limine memory map snapshot and first bump-only physical page allocator
  counters;
- [x] QEMU debug-exit poweroff through `exit`;
- [x] cooperative kernel-managed processes with PID, state, run count, exit
  code, `run [name]`, and `ps`.

Arwill does not yet have real disk I/O, storage-backed filesystems, interrupts,
a timer, preemptive scheduling, user-space isolation, syscalls, ELF program
loading, or writable persistent storage.

## Milestones

1. [x] Kernel cooperative processes

   Status: done in `0.1.0`.

   Arwill can launch built-in cooperative kernel processes with `run [name]`
   and inspect them with `ps`. These are kernel-managed run-to-completion work
   units, not user-space processes.

   Verified by: QEMU smoke test for `run hello`, process output, `ps`, and
   successful `exit` poweroff.

2. [ ] Block device reads

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

   Expected tests:

   - `make check` still boots and powers off;
   - smoke test observes block device initialization;
   - smoke test verifies bytes read from a known sector in the test image;
   - negative path for out-of-range or unavailable reads returns an error
     instead of hanging.

   Definition of done: Arwill can read a known sector from a real QEMU-attached
   image without any filesystem parser involved.

3. [ ] Real read-only filesystem

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

   Expected tests:

   - smoke test lists directories from the real image;
   - smoke test reads real file contents through `cat`;
   - smoke test checks `stat` for real file and directory metadata;
   - negative tests cover missing files, missing directories, and binary files.

   Definition of done: the shell's filesystem commands no longer depend on
   hard-coded directory entries for the primary happy path.

4. [ ] Interrupts, timer, and scheduler foundation

   Goal: create the execution foundation needed to move beyond
   shell-triggered run-to-completion kernel processes.

   Scope:

   - add IDT setup and basic exception reporting;
   - add a timer source;
   - add saved execution context structures;
   - add a simple scheduler path for kernel tasks;
   - keep behavior deterministic enough for smoke tests.

   Expected tests:

   - smoke test observes interrupt and timer initialization;
   - deliberate safe exception or diagnostic path reports through the serial
     console;
   - scheduler diagnostics show that more than one kernel task can make
     progress;
   - tests do not depend on fragile host timing.

   Definition of done: Arwill can schedule kernel tasks independently of a
   single shell command running a function to completion.

5. [ ] User-space v1

   Goal: run the first isolated user-mode program.

   Scope:

   - add the required descriptor/user-mode entry groundwork;
   - add a minimal ELF loading path or a deliberately simpler first executable
     format if documented;
   - add a minimal syscall ABI, starting with `write` and `exit`;
   - connect process exit status to the process table.

   Expected tests:

   - smoke test launches a tiny user program;
   - program output reaches the serial console only through the syscall path;
   - process exit status is observable through `ps` or another documented
     command;
   - invalid user behavior does not crash the kernel silently.

   Definition of done: Arwill can run a separate user-mode program with a clear
   kernel/user boundary.

6. [ ] Writable filesystem

   Goal: add controlled persistent write support after read storage, filesystem
   reads, scheduling direction, and user-space basics are established.

   Scope:

   - add block write support with error reporting;
   - evolve the filesystem contract for create, overwrite, append, or whichever
     first write operations are explicitly chosen;
   - define allocation/update rules and persistence semantics;
   - keep write operations narrow until crash consistency and caching are better
     understood.

   Expected tests:

   - create or modify a small file and read it back in the same boot;
   - verify persistence across a rebooted QEMU session with a test disk image;
   - cover write failure paths such as no space or invalid path;
   - keep read-only filesystem tests passing.

   Definition of done: Arwill can persist a small file change to a QEMU disk
   image and observe it again after reboot.
