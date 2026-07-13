# ADR-0045: Configuration and Event Log v1

Status: accepted

## Context

The remote console now has owner-controlled operating parameters, and process
and connection lifecycle events need one inspectable diagnostic history. Fixed
constants spread through network and shell code would hide policy, while a
daemon, syslog protocol, or configuration framework would be disproportionate.

## Decision

Store configuration in `/owner/arwill.conf` as at most 1024 bytes of ASCII
`key=value` data. Version 1 accepts only `config.version`, `remote.enabled`,
`remote.port`, `remote.key`, and `log.level`. Reject malformed, duplicate, and
unknown keys. Missing configuration uses compiled defaults; malformed existing
configuration disables the remote service.

Expose one canonical `config` command. With no arguments it prints all values
and masks the access key. `config <key> <value>` persists a non-secret value;
`config remote.key` reads a replacement without echo. There are no show, get,
set, reload, import, section, include, or environment commands.

Add a fixed 64-entry in-memory event ring. Each entry contains monotonic
milliseconds, severity, subsystem, event code, and two numeric arguments.
Overwrite the oldest entry when full. The only command is `logs`, which prints
the complete ring in chronological order. Never record keys, input bytes,
commands, or file contents. Do not write every event to ARFS because ARFS has no
journal or atomic append.

## Consequences

Operational policy has one small durable source and diagnostics remain bounded.
The v1 event history is lost on reboot and its timestamps are uptime, not wall
clock time.

## Verification

Verify defaults, persistence, masked key display, invalid-config fail-safe
behavior, ring wraparound, and absence of secret bytes in `logs` output.
