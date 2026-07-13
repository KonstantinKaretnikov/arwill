# ADR-0037: SSH Cryptographic Foundation

Status: superseded by ADR-0041

## Context

Arwill has a verified Ethernet, IPv4, TCP, and SSH version-exchange path. A
real SSH transport now requires constant-time, reviewed cryptographic
primitives; implementing them ad hoc in the kernel would be unsafe.

## Decision

Use a separate Arwill crypto layer backed by deliberately small, individually
imported primitive subsets from BearSSL 0.6 under its MIT License. Never import
the full BearSSL TLS/X.509 surface. The selected primitives are SHA-256,
Curve25519 ECDH, P-256 ECDSA, and ChaCha20-Poly1305. The first SSH algorithm set
will therefore be
`curve25519-sha256`, `ecdsa-sha2-nistp256`, and
`chacha20-poly1305@openssh.com`.

Ed25519 is not part of this first implementation because BearSSL 0.6 does not
provide EdDSA. The initial owner model remains a single authorized public key,
with no passwords or multi-user accounts.

## Consequences

The repository must retain BearSSL copyright and MIT license text, document
the exact imported source subset, and run known-answer vectors before it is
used for network authentication. A platform entropy source is still required
before generating ephemeral SSH secrets or host keys.

## Verification

Each imported primitive will have a local known-answer test. SSH integration is
not considered complete until an OpenSSH client completes key exchange against
the QEMU guest.
