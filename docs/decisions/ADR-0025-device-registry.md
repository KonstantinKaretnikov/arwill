# ADR-0025: Tiny Device Registry

Status: Accepted

## Context

Arwill now has several platform and kernel contracts: serial console/input,
block storage, filesystem, interrupts/timer, poweroff, user runtime, and a
kernel heap. For board experiments, the owner needs a direct way to inspect
what the OS detected and published.

A full driver model, bus hierarchy, hotplug system, or dynamic probing policy
would be premature for the current simple single-owner OS.

## Decision

Add a fixed-size device registry with entries containing:

- name;
- kind;
- driver;
- status.

The x86-64/QEMU boot path registers the current serial, input, disk,
filesystem, heap, timer/interrupts, power, and user-runtime contracts. The shell
adds `devices` to print the registry.

The registry is an inspection table. It does not own driver lifetimes, mediate
access, model buses, or perform hotplug.

## Consequences

Arwill can now show the owner the current device surface in one command. Future
board ports can add entries without building a large driver model first.

If later work needs typed handles, probing order, dependency management, or
dynamic device creation, that should be introduced as a separate milestone and
ADR.
