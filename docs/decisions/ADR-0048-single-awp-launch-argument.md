# ADR-0048: Single AWP Launch Argument

Status: accepted

## Context

The editor originally asked for its path after launch because Arwill Program
v1 had no argument channel. This made the natural command `exec
/apps/edit.awp /owner/arwill.conf` silently ignore the file path and then ask
for it again. Adding POSIX `argc`/`argv`, quoting, an environment, or a process
startup stack would be disproportionate to the current use case.

## Decision

Extend `exec` to accept at most one optional whitespace-delimited launch
argument: `exec [image] [argument]`. Store at most 63 ASCII bytes in the fixed
AWP task slot. Syscall `7` copies the current task's launch argument into a
bounded writable user buffer and returns its byte length. It returns zero when
the launcher supplied no argument and `-1` when the destination is too small
or invalid.

The argument is opaque to the shell: it is not quoted, expanded, or resolved
against the shell working directory. Multiple arguments are rejected. This
does not change the on-disk `AWP1` image format.

Make `/apps/edit.awp` require one absolute file path through this channel.
Remove its interactive `edit file:` prompt. With no argument it prints `edit:
missing file` and exits with status `2`; a relative path produces an explicit
absolute-path error.

## Consequences

The owner can open the configuration directly with `exec /apps/edit.awp
/owner/arwill.conf`, while other AWP programs may ignore the bounded argument.
Arwill still has no general argument vector, quoted arguments, spaces inside an
argument, environment variables, inherited working directory, or POSIX process
startup ABI.

## Verification

Verify that launching the editor without a file reports the missing-file error,
that direct launch opens and saves a text file, and that `/owner/arwill.conf`
opens with its existing contents. Keep the concurrent editor/calculator smoke
scenario on the direct-launch path.
