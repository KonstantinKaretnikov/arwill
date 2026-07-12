# ADR-0019: Interrupts, Timer, and Scheduler Foundation

Status: accepted

## Context

Arwill has cooperative kernel-managed processes, a shell, block reads, and a
storage-backed read-only filesystem. Those pieces still run from direct shell
commands or boot-time initialization. Moving toward real scheduling requires an
interrupt path and a timer source before saved task contexts or user-space can
be implemented responsibly.

The current project rule is to prefer narrow verified milestones. A full
preemptive scheduler, APIC support, user-space entry, and syscall boundary
would make this step too large.

## Decision

Add a minimal x86-64 interrupt foundation for the QEMU target:

- install an IDT;
- remap the legacy PIC to vectors `0x20` through `0x2f`;
- unmask IRQ0 only;
- configure the PIT at 100 Hz;
- handle breakpoint vector 3 as a safe exception diagnostic;
- handle timer vector 32 and call `arwill_scheduler_tick()`.

Add an architecture-independent interrupt contract so kernel and shell code can
query status, enable interrupts, wait for a timer tick, and trigger the
breakpoint diagnostic without depending on x86-64 port details.

Add a scheduler foundation that records timer ticks and alternates accounting
between two named slots, `shell` and `idle`. Expose the new behavior through
canonical shell commands: `irqinfo`, `irqprobe`, and `schedinfo`.

## Consequences

Arwill now has real hardware interrupt handling in the QEMU/x86-64 path, an
observable PIT timer, and a timer callback into scheduler code. The bounded QEMU
smoke test verifies startup, timer observation, the breakpoint diagnostic, and
scheduler tick accounting without relying on exact tick counts.

This does not make Arwill preemptive. There are still no saved task contexts,
kernel stack switches, sleeping tasks, priority rules, user-space programs,
syscalls, or privilege transitions.

## Alternatives Considered

APIC and LAPIC timer support were deferred because the legacy PIC and PIT are
sufficient for the first QEMU-only interrupt milestone and simpler to verify.

Implementing saved contexts and context switching in the same change was
deferred to keep the system bootable and the tests focused.

Keeping timer behavior as a host-side or shell-only placeholder was rejected
because it would not prove the hardware interrupt path.

## Revisit

Revisit when Arwill needs saved kernel task contexts, preemptive context
switching, sleeping or blocked task states, user-space entry, syscall delivery,
APIC/IOAPIC support, LAPIC or HPET timers, SMP, or a generic interrupt
registration model.
