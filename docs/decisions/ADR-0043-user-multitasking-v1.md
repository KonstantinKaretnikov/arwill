# ADR-0043: User Multitasking v1

Status: accepted

## Context

Arwill can enter ring 3 and execute an AWP image, but execution is synchronous.
All programs reuse one user mapping, the `read` syscall is wired to blocking
serial input, and the shell cannot serve another session until the program
exits. The owner now needs one interactive AWP in the serial session and one in
the TCP session at the same time.

## Decision

Add a fixed four-slot AWP task table and save the complete x86-64 user context
on kernel transitions. Each slot owns a preallocated, reusable address space
with supervisor-only kernel mappings, read/execute code pages, read/write and
non-executable stack pages, and an unmapped stack guard page.

The 100 Hz PIT may preempt ring 3 after a fixed two-tick quantum. It never
switches tasks while executing ring 0. A small round-robin dispatcher resumes
one ready AWP from the shell service loop. Syscall `read` blocks only the
calling AWP and resumes it after input reaches its originating shell session.

AWP states are `empty`, `ready`, `running`, `blocked-input`, `finished`, and
`faulted`. User invalid-opcode, general-protection, and page faults terminate
only the active AWP. Kernel faults remain fatal. A session has at most one
foreground AWP; Ctrl+C cancels it and a TCP disconnect cancels its remote AWP.

Keep cooperative kernel built-ins as their existing, distinct process kind.
`ps` displays both kinds. Do not add `jobs`, `fg`, `bg`, priorities, signals,
SMP, kernel preemption, fork, independent kernel stacks, or ELF.

## Consequences

Two interactive AWP images can make progress independently on one vCPU and
cannot overwrite each other's code or stack. A CPU-bound AWP no longer owns the
shell indefinitely. Fixed preallocation avoids adding a physical-page release
mechanism solely for this milestone.

The dispatcher still runs from the shell loop, each session still has only one
foreground program, and kernel code can delay a user task until it returns to
the dispatcher.

## Verification

Run the calculator in one session and the editor in the other, verify both
remain responsive, cancel either with Ctrl+C, verify `ps`, and run a faulting
AWP without losing either shell.
