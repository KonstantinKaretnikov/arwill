# ADR-0048: Single AWP Launch File Path

Status: accepted

## Context

The editor originally asked for its path after launch because Arwill Program
v1 had no argument channel. This made the natural command `exec
/apps/edit.awp /owner/arwill.conf` silently ignore the file path and then ask
for it again. Adding POSIX `argc`/`argv`, quoting, an environment, or a process
startup stack would be disproportionate to the current use case.

## Decision

Extend `exec` to accept at most one optional launch file path: `exec [image]
[file]`. Both positions support filesystem Tab completion. Resolve a relative
file path against the launching shell session's current directory and pass the
canonical absolute path to the AWP task. Store at most 63 ASCII bytes in its
fixed slot. Syscall `7` copies the path into a bounded writable user buffer and
returns its byte length. It returns zero when the launcher supplied no file and
`-1` when the destination is too small or invalid.

The path is not quoted or expanded, and multiple launch values are rejected.
This does not change the on-disk `AWP1` image format.

Make `/apps/edit.awp` require one file path through this channel; the shell
delivers it in canonical absolute form. Remove its interactive `edit file:`
prompt. With no file it prints `edit: missing file` and exits with status `2`.

## Consequences

The owner can open the configuration directly with `exec /apps/edit.awp
/owner/arwill.conf` or, from `/owner`, with `exec /apps/edit.awp arwill.conf`.
Other AWP programs may ignore the bounded path. Arwill still has no general
argument vector, quoted arguments, spaces inside an argument, environment
variables, inherited working-directory state inside the task, or POSIX process
startup ABI.

## Verification

Verify that launching the editor without a file reports the missing-file error,
that Tab completes both `exec` paths, that a relative file is resolved against
the shell current directory, and that `/owner/arwill.conf` opens with its
existing contents. Keep the concurrent editor/calculator smoke scenario on the
direct-launch path.
