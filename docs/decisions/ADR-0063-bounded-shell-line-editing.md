# ADR-0063: Bounded Shell Line Editing

Status: accepted

## Context

The shell already recognizes terminal escape sequences for Up and Down command
history, but ignores standard Left and Right sequences. Correcting a typo in
the UTM serial console therefore requires erasing the rest of the command.

Arwill needs convenient configuration and command entry, but does not need a
general terminal subsystem or readline-style editor.

## Decision

Store one cursor index alongside each shell session's fixed-size input line.
Accept both CSI (`ESC [`) and SS3 (`ESC O`) Left and Right sequences. Printable
input inserts at the cursor, and Backspace removes the byte before the cursor.
Redraw only the bounded suffix affected by an edit.

History recall replaces the current line and places the cursor at its end. Tab
completion remains active only when the cursor is at the end of the line. The
serial and authenticated remote shells share this behavior through the
canonical shell byte handler.

## Consequences

Commands can be corrected in place in UTM and other compatible raw terminals.
All state remains fixed-size, per-session, and shell-local. No new dependency or
cross-layer terminal contract is introduced.

Delete-forward, Home/End, word movement, selection, undo, multiline input,
quoted argument parsing, and a general terminal or readline abstraction remain
out of scope.

## Verification

The QEMU serial smoke sends cursor escape sequences and verifies commands fixed
by mid-line Backspace and insertion before running the full existing suite.

## Revisit

Revisit only when a current shell workflow requires another editing operation
or Arwill gains a real terminal abstraction.
