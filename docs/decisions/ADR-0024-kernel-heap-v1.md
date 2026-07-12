# ADR-0024: Small HHDM-Backed Kernel Heap

Status: Accepted

## Context

Arwill's first memory allocator was a physical page bump allocator. That was
enough for early boot, user-mode page setup, and simple counters, but it forced
kernel code to rely on static arrays or whole-page allocations.

The project needs a small dynamic allocator for simple kernel objects. It does
not yet need a slab allocator, demand paging, userspace heap, page reclamation,
or a full virtual memory policy.

## Decision

Add a narrow kernel heap built from a small contiguous run of physical pages
reserved during boot. The heap is accessed through Limine HHDM and managed by a
simple free list.

The public memory contract now exposes:

- `arwill_kernel_heap_init`;
- `arwill_kmalloc`;
- `arwill_kfree`;
- `arwill_kernel_heap_stats`.

The first heap can split free blocks, coalesce adjacent free blocks on free, and
report counters through `meminfo`. The shell command `heaptest` allocates and
frees two small blocks so the behavior is covered by the QEMU smoke test.

## Consequences

Kernel code can now allocate small dynamic objects without inventing local
static pools for every subsystem.

The heap currently depends on HHDM and a contiguous physical page run from the
bump allocator. If the physical allocator later stops returning contiguous
pages, heap initialization should either gather page runs deliberately or move
to page-by-page virtual mapping.

This is not a general virtual memory subsystem and does not release heap pages
back to the physical allocator.
