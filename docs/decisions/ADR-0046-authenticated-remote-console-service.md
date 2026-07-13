# ADR-0046: Authenticated Remote Console Service

Status: accepted

## Context

The plaintext console is useful through localhost forwarding, but the owner now
wants to connect from another trusted LAN machine. Unconditionally exposing the
owner shell would be unsafe, while completing SSH or TLS would reintroduce the
large cryptographic subsystem deliberately removed by ADR-0041.

## Decision

Treat `remote-console` as the first built-in service. It has only `running`,
`stopped`, `failed`, and `unavailable` states and supports `service status`,
`service start remote-console`, `service stop remote-console`, and `service
restart remote-console`. Boot startup follows `remote.enabled`; do not add
enable/disable, unit files, dependencies, respawn, service accounts, dynamic
registration, or AWP services.

Listen on `remote.port`, default 23232. Before showing the shell, request
`remote.key` without echo, compare it in constant time, allow three attempts,
and close failed sessions. Empty keys do not permit remote login. Record
connect, authentication, disconnect, timeout, and service lifecycle events but
never the key.

QEMU's host bind belongs to the host launcher, not guest configuration. Keep
`127.0.0.1` as the default and permit an explicit `QEMU_REMOTE_CONSOLE_BIND`
override such as `0.0.0.0`. Keep host and guest port parameters distinct and
document that the forwarded guest port must match `remote.port`.

## Consequences

The key is an access gate suitable for a trusted LAN, not secure transport. The
key and all shell traffic remain observable on the network. Internet exposure
is unsupported; use a host SSH tunnel when confidentiality is required.

## Verification

Verify wrong and correct keys, connection from an explicitly non-loopback host
bind, service stop/start/restart, disconnect during restart, boot policy, and
event-log coverage.
