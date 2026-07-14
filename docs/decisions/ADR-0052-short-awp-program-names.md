# ADR-0052: Short AWP Program Names

Status: accepted

## Context

Stored applications live in the fixed `/apps` directory and use the fixed
`.awp` extension, but every launch currently repeats both details. Commands
such as `exec /apps/calc.awp` expose storage layout that is stable by project
decision and make the common interactive path unnecessarily long.

Arwill still has no shell configuration, environment, executable permissions,
or need for a general program search path. Adding aliases or `PATH` would
broaden the shell and conflict with ADR-0015's canonical-command rule.

## Decision

Release short AWP program names as Arwill `0.17.2` within the existing
canonical `exec` command.

When the image argument contains no `/` and does not end in `.awp`, resolve it
exactly as `/apps/<name>.awp`. Thus `exec calc` launches
`/apps/calc.awp`, and `exec edit /owner/note` launches `/apps/edit.awp` with
the existing single launch-file path from ADR-0048.

Preserve existing explicit path behavior. An argument containing `/` uses
normal filesystem resolution, and a bare argument ending in `.awp` remains a
relative image path. There is no fallback: a missing short name reports the
resolved `/apps` path and is not searched elsewhere.

For the first `exec` position, Tab completes file names from `/apps` that end
in `.awp`, displaying and inserting them without the extension. An input that
contains `/` or `.` uses ordinary path completion. The second position keeps
ordinary filesystem path completion.

## Consequences

Common application launches become concise while `exec` remains the only
public launch command. Scripts and diagnostics using explicit paths continue
to work, and the spawned task still records the canonical absolute image path.

This does not add aliases, `PATH`, multiple search directories, extension
probing, executable metadata, general arguments, quoting, or an environment.

## Verification

Build Arwill and run native tests plus the QEMU serial/TCP smoke path. Launch a
short name directly, complete a short name without `.awp`, pass a relative file
to `edit`, repeat a short launch over TCP, and retain one explicit image-path
launch after reboot.
