# ADR-0040: SSH Curve25519 ECDH Reply

Status: accepted

## Context

Arwill exchanges SSH identification strings and KEXINIT packets, has a
persistent P-256 host key, and can produce deterministic ECDSA signatures. An
OpenSSH client next sends `SSH_MSG_KEX_ECDH_INIT`; without a standards-compliant
reply, transport key exchange cannot advance.

The exchange hash can include a client KEXINIT payload near the transport's
4096-byte limit. Building one contiguous hash input on the small kernel stack
would be unnecessary and unsafe.

## Decision

Implement the `curve25519-sha256` exchange described by RFC 8731, using the
ECDH message and exchange-hash structure from RFC 5656:

- accept only an exact 32-byte client ephemeral public value;
- generate a fresh server scalar through the entropy contract;
- derive the server public value and shared secret through the X25519 crypto
  contract, including its all-zero shared-secret rejection;
- encode the shared secret as a positive SSH `mpint` without reversing its
  RFC 7748 byte string;
- hash `V_C`, `V_S`, `I_C`, `I_S`, `K_S`, `Q_C`, `Q_S`, and `K` in order;
- sign the exchange hash with the persistent P-256 host key;
- encode ECDSA `r` and `s` as nested positive SSH `mpint` values;
- send `SSH_MSG_KEX_ECDH_REPLY` containing the host-key blob, server ephemeral
  value, and signature blob.

Add an opaque streaming SHA-256 context to the Arwill crypto contract. Retain
the shared secret and exchange hash in explicit transport state for the later
key-derivation step, while clearing the ephemeral server private scalar as
soon as X25519 completes. Keep bounded construction scratch space in the
transport object rather than on the kernel stack.

## Consequences

The server can authenticate a Curve25519 exchange through its persistent host
identity. Malformed public-value lengths, low-order points, entropy failure,
encoding overflow, signing failure, and send failure stop the exchange.

This milestone does not send or process `SSH_MSG_NEWKEYS`, derive transport
keys, or enable packet encryption. The connection therefore cannot progress
to authentication yet.

## Verification

The QEMU smoke test exercises fragmented SHA-256 input, parses a synthetic
`SSH_MSG_KEX_ECDH_INIT`, constructs the full reply, checks its message framing,
and verifies that the test does not corrupt the boot memory map. Real OpenSSH
interop remains the final wire-level check before this step is treated as an
end-to-end transport result.
