# Arwill Roadmap

This roadmap records the current agreed order for the next large operating
system milestones. It is intentionally sequential: finish and verify one layer
before depending on it from the next layer.

## Working Rule

Prefer more tests and smaller verified steps over broad subsystem jumps. Every
milestone should keep Arwill bootable, update documentation, and extend the
bounded QEMU smoke test or add another suitable automated check.

## Milestones

1. Kernel cooperative processes

   Status: done in `0.1.0`.

   Arwill can launch built-in cooperative kernel processes with `run [name]`
   and inspect them with `ps`. These are not user-space processes.

2. Block device reads

   Add a narrow block device contract and the first QEMU-backed read-only
   storage driver. The first goal is reading real sectors, not parsing a full
   filesystem.

   Expected tests: boot smoke coverage for device discovery and a deterministic
   sector read from a known test image.

3. Real read-only filesystem

   Replace the static boot catalog with a filesystem implementation backed by
   block storage. Candidate first formats are ISO9660 for the boot image or a
   simple initrd/tar format.

   Expected tests: `ls`, `cd`, `cat`, and `stat` should read real files through
   the filesystem path, with negative tests for missing files and directories.

4. Interrupts, timer, and scheduler foundation

   Add IDT setup, interrupt handlers, a timer source, saved execution contexts,
   and a simple scheduler path. This should turn process execution from
   shell-triggered run-to-completion work into scheduled kernel tasks.

   Expected tests: boot smoke output for initialized interrupt/timer blocks and
   deterministic scheduler observations that do not depend on host timing.

5. User-space v1

   Add the first user-mode program path: ELF loading, user/kernel privilege
   transition, and a minimal syscall ABI such as `write` and `exit`.

   Expected tests: launch a tiny user program, observe its output through the
   syscall path, and verify its exit status without crashing the kernel.

6. Writable filesystem

   Add controlled write support after the read path, block layer, and process
   direction are better established. This includes filesystem write contracts,
   allocation or update rules, write error handling, and persistence semantics.

   Expected tests: create or modify a small file, read it back in the same boot,
   and eventually verify persistence across a rebooted QEMU session with a test
   disk image.
