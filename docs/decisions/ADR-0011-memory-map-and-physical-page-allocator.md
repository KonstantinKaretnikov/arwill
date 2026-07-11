# ADR-0011: Memory Map and Physical Page Allocator

Status: accepted

## Context

Arwill needs to know which physical memory ranges are usable before it can grow
toward heaps, process memory, file buffers, or drivers. Limine already provides
a boot memory map, but architecture-independent kernel code should not depend on
Limine structs.

## Decision

Request the Limine memory map in the x86-64 boot block and convert it into an
Arwill-owned `struct arwill_memory_region` snapshot before entering
architecture-independent kernel startup.

Add a kernel memory contract that exposes:

- the converted boot memory map;
- stable names for memory region types;
- physical page allocator counters;
- a first `arwill_physical_allocate_page` operation.

The first physical allocator is bump-only. It page-aligns Limine `usable`
ranges, hands out 4096-byte physical pages in ascending order, and tracks total,
free, allocated, and allocation counters. It intentionally has no `free`, no
bitmap, no heap dependency, and no virtual mapping behavior.

Add `meminfo` to the shell to display the memory map and allocator counters.

## Consequences

Arwill now has a real boot-time view of physical memory and a minimal allocator
surface for future subsystems. The implementation remains honest about what it
does not do: no memory reclamation, no page table management, no heap, and no
general virtual memory manager.

The Limine dependency remains in the x86-64 boot layer. Kernel and shell code
depend only on the Arwill memory contract.

## Alternatives Considered

Keeping Limine memory map structs in kernel code was rejected because it would
leak bootloader details across the architecture boundary. A bitmap allocator was
deferred because it needs storage for the bitmap and clearer ownership of early
kernel memory. A heap allocator was rejected because physical page allocation is
the smaller prerequisite.

## Revisit

Revisit when Arwill adds a higher-half direct map, page table management, a
kernel heap, page freeing, or bootloader-reclaimable memory reclamation.
