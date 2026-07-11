# ADR-0004: Selected Boot Strategy

Status: accepted

## Context

The first commit needs a maintained boot path suitable for an x86-64 hobby
kernel in QEMU. Writing a custom BIOS or UEFI bootloader would consume the
milestone and obscure Arwill-owned kernel architecture.

## Decision

Use Limine v12.4.2 as external boot infrastructure, fetched by `make setup`
from pinned release archives and verified by SHA-256. Build a BIOS/UEFI hybrid
ISO using the documented Limine ISO flow.

Limine belongs to `third_party/limine/` and is not Arwill-owned source code.
Arwill owns the kernel, contracts, platform serial block, Limine config, build
scripts, and documentation.

## Consequences

Arwill reaches 64-bit kernel code without implementing boot firmware, a
filesystem, a keyboard driver, or a custom bootloader. The repository depends
on external boot infrastructure and on `xorriso` for ISO creation.

Replacing Limine later should require a new boot adapter, build artifact, and
ADR, not changes to architecture-independent kernel startup.

## Alternatives Considered

A custom bootloader was rejected for the first commit because it is not the
product being tested. GRUB/Multiboot2 was considered, but it would either add
different host dependencies or require more early CPU setup code. QEMU direct
kernel loading was rejected because the milestone calls for an established
external bootloader.

## Revisit

Revisit if Limine no longer meets project needs, if its protocol churn becomes
too costly, or when Arwill needs boot-time services Limine cannot provide.
