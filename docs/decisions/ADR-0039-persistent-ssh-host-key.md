# ADR-0039: Persistent SSH Host Key

Status: superseded by ADR-0041

## Context

An SSH server must retain the same host identity across boots. Generating a new
key for every connection or boot would prevent the owner from reliably
recognizing the machine. Arwill already has fail-closed entropy, P-256 public
point derivation, and an ARFS v2 whole-file binary write contract.

The existing ARFS v2 image uses 13 of its 16 fixed entries. Persisting one host
key while retaining the mutable-filesystem smoke scenario requires slightly
more table capacity, but does not require a new filesystem format.

## Decision

Store one 32-byte P-256 private scalar as the binary ARFS file
`/system/ssh-host-key`. On first boot, generate a valid scalar through the
architecture-independent entropy contract, derive its public point, and write
the private scalar before making the key available to SSH. On later boots,
load and validate the existing file. An existing malformed key is an error and
must not be silently replaced.

Expose only a hexadecimal SHA-256 digest of the standard SSH
`ecdsa-sha2-nistp256` public-key blob as a diagnostic fingerprint. Pass the
initialized host-key object explicitly into the IPv4/SSH path; storage access
does not belong in the network layer.

Raise the fixed ARFS v2 entry and listing capacities from 16 to 20. All other
ARFS v2 limits and semantics remain unchanged.

## Consequences

Arwill has a stable host identity suitable for the upcoming ECDH reply. The
private key remains a small Arwill-internal binary representation, not an
OpenSSH PEM file. Under the single-owner model it is not protected by Unix-like
permissions, and ARFS still provides no crash consistency or atomic metadata
replacement.

SSH remains unavailable if entropy, storage, validation, or persistence fails.
The key is not yet used to sign a live exchange hash; that belongs to the ECDH
reply milestone.

## Verification

The QEMU smoke test observes first-boot creation, confirms a 32-byte binary
ARFS entry, reboots with the same disk, observes loading, and compares the
public fingerprint across both boots.
