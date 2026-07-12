# ADR-0026: Framebuffer Text Console Mirror

Status: Accepted

## Context

Arwill is intended to remain a simple OS for boards and experiments. Serial is
excellent for automated QEMU smoke tests, but a visible text console is useful
when bringing the system up on screens or board-style setups.

The project does not need a graphics subsystem, terminal emulator, windowing
layer, font loader, or input focus model yet.

## Decision

Request a Limine framebuffer and add an x86-64 framebuffer text console. The
framebuffer console mirrors the serial console: existing boot and shell output
still goes to serial, and the same text is drawn into the framebuffer when a
32-bit framebuffer is available.

The first renderer uses a small built-in 5x7 bitmap font, handles printable
ASCII, newlines, carriage returns, and backspace, and clears the screen when the
text reaches the bottom.

The device registry exposes this path as `fb0`.

## Consequences

Arwill now has visible screen output without changing the serial-first test
workflow. QEMU smoke tests remain serial-authoritative.

The screen output is deliberately basic. Future work can add a scrollback
buffer, richer glyph coverage, color control, or a separate graphics contract,
but those should be separate milestones.
