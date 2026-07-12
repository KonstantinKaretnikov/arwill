# ADR-0038: SSH Entropy v1

Status: accepted

## Context

SSH key exchange requires unpredictable ephemeral secrets. PIT ticks and other
deterministic boot state are not cryptographic entropy. Arwill currently runs
only on x86-64/QEMU and has no virtio RNG driver or persistent seed store.

## Decision

Use the x86-64 `RDRAND` instruction as the first narrow entropy source. Check
CPUID before every public request, retry each 64-bit acquisition at most 16
times, and fail closed by clearing the requested output if acquisition fails.

Pin QEMU development and smoke runs to the `max` CPU model so this first target
explicitly exposes `RDRAND`. Do not silently fall back to PIT ticks, device
timing, or uninitialized memory.

The architecture-independent entropy contract exposes availability and a
bounded byte-fill operation. The x86-64 implementation remains below that
contract; SSH and crypto code must not issue the instruction directly.

## Consequences

SSH KEX can generate ephemeral secrets on the supported QEMU target and can
refuse to proceed when entropy is unavailable. This is not yet a general
hardware RNG design, a seeded DRBG, health-testing framework, persistent seed
store, or support for physical machines without `RDRAND`.

## Verification

The QEMU smoke test checks CPUID availability and successfully acquires 32
bytes without printing the sample. Statistical randomness testing is
intentionally not claimed.
