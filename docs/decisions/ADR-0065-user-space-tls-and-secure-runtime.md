# ADR-0065: User-Space TLS and Secure Runtime

Status: accepted

## Context

ADR-0064 deferred HTTPS until Arwill had explicit contracts for secure
randomness, wall-clock time, certificate trust, and a cryptographic-library
boundary. HTTPS GET and POST are now the next current consumer. Future SSH
will need some of the same cryptographic primitives, but TLS records, X.509
validation, and HTTP policy are not SSH mechanisms.

The experimental SSH work removed by ADR-0041 showed that protocol-specific
cryptography in the kernel creates the wrong dependency direction. This
decision partially revisits ADR-0041 only to introduce a current, reviewed
cryptographic dependency for user space. It does not restore the removed SSH
service or move a secure protocol into the kernel.

## Decision

Add two architecture-independent system contracts and expose them to AWP:

| `rax` | operation | arguments | result |
| --- | --- | --- | --- |
| 22 | `random_fill` | `rdi=writable buffer`, `rsi=1..256` | byte count or `-1` |
| 23 | `realtime` | none | Unix UTC seconds or `-1` |

The x86-64 entropy provider uses CPU `RDRAND`, retries boundedly, clears output
on failure, and fails closed when the instruction is unavailable. The QEMU
development profile exposes this CPU feature explicitly. The realtime provider
reads a stable CMOS RTC sample and converts Gregorian UTC fields to Unix
seconds. Neither contract contains TLS, X.509, HTTP, or SSH policy.

Vendor BearSSL 0.6 under its MIT license and pin the upstream archive SHA-256
in `third_party/bearssl/README.arwill.md`. Compile it freestanding with host
entropy and time discovery disabled. `libtls` injects entropy and validation
time through the AWP contracts, restricts negotiation to TLS 1.2 with
ECDHE-ECDSA or ECDHE-RSA and AES-128/256-GCM, performs SNI and hostname
verification, and validates the certificate chain. Vendor only the upstream
files reached by that client profile rather than the complete BearSSL source
archive.

The first trust store is deliberately bounded to Amazon Root CA 1 and SSL.com
TLS ECC Root CA 2022. It covers the initial `httpbin.org` and `example.com`
targets; it is not a complete Web PKI bundle. Verification must never be
disabled to make another site work. Root changes require review.

TLS remains a user-space library over the existing bounded nonblocking TCP
ABI. `curl` owns URL, HTTP, DNS, TLS session, certificate policy, and output.
The kernel owns packets, TCP endpoints, handle ownership, safe copying,
entropy, and time.

The TLS client exceeds the original two-page demo image. Expand each fixed AWP
slot to 48 code pages and 24 stack pages. Permit seeded binary images of at
most 192 KiB to be read and executed, while preserving the 8192-byte mutable
file limit and narrower whole-text-file syscall bound. This remains fixed
boot-time storage, not demand paging, a heap, ELF, or dynamic memory.

Because BearSSL contains static tables of function pointers, link `curl.awp`
statically at the AWP runtime's existing fixed code virtual address. The
loader still consumes a flat AWP image and applies no ELF or dynamic
relocations. Other position-independent AWP images remain valid.

## Future SSH Boundary

SSH may reuse entropy and realtime contracts, freestanding memory routines,
and reviewed BearSSL primitives such as hashes, HMAC, public-key operations,
and symmetric ciphers. It must implement its own transport, key exchange,
host-key storage and verification, packets, authentication, and channels in a
separate user-space component. It must not reuse TLS records, Web trust roots,
X.509 validation, or HTTP code.

## Consequences

HTTPS GET and POST can resolve a domain, connect through existing TCP,
negotiate verified TLS 1.2, and stream an HTTP/1.0 body. Plain HTTP remains.
The client has no TLS 1.3, full root store, redirects, chunked decoding,
compression, proxies, cookies, authentication, multipart forms, or persistent
connections.

CMOS adds UTC time but no timezone, NTP, date command, or writable clock.
`RDRAND` is the only entropy source in this milestone; unavailable hardware
makes HTTPS fail closed.

## Verification

Native tests cover the entropy contract, Gregorian conversion, HTTPS URL and
default-port formatting, and DNS/HTTP codecs. The normal build links the
freestanding TLS client. QEMU verification covers boot, existing smoke suites,
and live verified HTTPS GET and POST by domain name.
