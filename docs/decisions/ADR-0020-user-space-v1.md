# ADR-0020: User-Space v1

Status: accepted

## Context

Arwill now has a serial shell, cooperative kernel-managed processes, storage
reads, a read-only filesystem, an IDT, a PIT timer, and scheduler tick
accounting. The next roadmap milestone is the first user/kernel boundary.

A production user-space stack would require an ELF loader, per-process page
tables, richer memory management, saved task contexts, blocking states, and a
larger syscall surface. Implementing all of that at once would violate the
project rule to keep milestones narrow and verified.

## Decision

Add the smallest useful x86-64 user-space path:

- request Limine HHDM so the kernel can initialize allocated physical pages;
- install an Arwill-owned GDT with kernel and user descriptors;
- load a TSS with an `rsp0` stack for privilege transitions;
- map one user code page and one user stack page with user permissions;
- enter ring 3 with `iretq`;
- install a DPL 3 `int 0x80` syscall gate;
- support syscall `1` as `write` and syscall `2` as `exit`;
- generate tiny built-in machine-code user programs for the first executable
  format.

Expose this through existing process flow:

- `run userhello` writes `user hello: hello from ring 3` through syscall
  `write` and exits with code `7`;
- `run userbad` invokes an unknown syscall, which exits with code `127` and
  increments the bad-syscall counter;
- `ps` reports user program exit status through the existing process table;
- `userinfo` reports user-mode setup and syscall counters.

## Consequences

Arwill now has a real CPU privilege transition to ring 3 and a tested first
syscall boundary. User output reaches the serial console through the syscall
handler, and user exit status is observable through the process table.

The implementation is intentionally narrow. There is no ELF loader, no
file-backed executable format, no per-process address space, no process
arguments, no user heap, no syscall pointer-copy abstraction, no saved user
contexts, and no preemptive user scheduling. User code and stack pages are
allocated from the bump allocator and are not freed.

## Alternatives Considered

Starting with ELF was deferred because executable loading would obscure the
first privilege-transition and syscall work behind parser and relocation
details.

Using `syscall`/`sysret` was deferred in favor of `int 0x80` because the IDT
path already exists and is easier to inspect in the current architecture.

Simulating user programs in a kernel interpreter was rejected because it would
not prove a real kernel/user CPU privilege boundary.

## Revisit

Revisit when Arwill needs ELF loading, per-process page tables, argument
passing, richer syscall validation, process memory cleanup, saved contexts,
preemptive user scheduling, or a switch from `int 0x80` to `syscall`/`sysret`.
