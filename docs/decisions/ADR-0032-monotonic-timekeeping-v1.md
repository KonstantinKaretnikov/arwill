# ADR-0032: Monotonic Timekeeping v1

Status: accepted

## Context

Arwill has a working 100 Hz PIT interrupt counter, but only exposes raw timer
diagnostics. Kernel and AWP code need a small stable time source without
introducing calendar policy or a general timer subsystem.

## Decision

Add an architecture-independent `clock` contract that returns monotonic
milliseconds. The first x86-64/QEMU implementation converts the existing PIT
tick count at 100 Hz, giving a resolution of 10 milliseconds.

Expose the value through:

- shell command `uptime`, printed as monotonic milliseconds since PIT
  initialization;
- syscall `4` (`clock`), returned directly in `RAX`.

## Consequences

Kernel and stored user programs have one consistent non-decreasing time base.
It advances while ring 3 code runs because hardware interrupts remain enabled
across user entry and syscall exit.

This clock is not wall time. There is no RTC/CMOS read, date, timezone, leap
second handling, NTP, alarms, sleeping processes, or per-process CPU time.

## Verification

The QEMU smoke path reads `uptime` before and after user-mode/AWP execution,
requires the second millisecond value to be greater than the first, and runs a
temporary AWP probe that exits successfully only when syscall `4` returns a
non-zero value.
