# ADR-0064: User-Space Web Client Boundaries

Status: accepted

## Context

Arwill 0.23.1 has fixed IPv4, a bounded TCP endpoint table, active TCP opens,
and a nonblocking AWP networking ABI. It can connect to an IPv4 address, but
AWP programs cannot resolve domain names and there is no HTTP client.

The desired first web surface is basic HTTP GET and POST by domain name.
Putting DNS, URL parsing, HTTP formatting, or a `curl` command in the kernel
would couple application protocols to network devices and transport state.

The current `exec` argument is deliberately a single filesystem path. Changing
it silently into general command-line text would break ADR-0048 and the editor
contract.

## Decision

Keep these responsibilities in the system layer:

- the e1000 device and Ethernet framing;
- ARP and IPv4;
- bounded TCP streams and a new bounded UDP datagram transport;
- handle ownership, user-memory validation, progress, and cleanup syscalls.

Keep these responsibilities in user space:

- DNS message encoding, response validation, and hostname resolution;
- URL parsing;
- HTTP/1.0 request formatting and response handling;
- the `curl` application and its user interaction.

Implement the web path in sequential increments:

1. Add freestanding user-space DNS and HTTP protocol libraries with native
   tests. Support only `http://`, DNS A records, GET, POST, explicit ports,
   `Host`, `Content-Length`, and `Connection: close`.
2. Add one bounded UDP datagram contract and AWP ABI sufficient for DNS.
   It must use fixed storage, nonblocking results, owner cleanup, and the
   existing network progress task.
3. Add a user-space DNS resolver over UDP. The first application uses
   QEMU/UTM synthetic `10.0.2.3` endpoint first and falls back to `1.1.1.1`.
   Moving resolver addresses into owner network configuration is a follow-up
   and does not move DNS policy into the kernel.
4. Add `/apps/curl.awp`. Its first interface is interactive so the existing
   file-path launch argument remains unchanged: it asks for method, URL, and
   a bounded POST body.
5. Add a QEMU integration fixture that proves DNS resolution followed by HTTP
   GET and POST through the real AWP ABI.

HTTP responses are bounded and streamed to the application console. The first
version accepts identity bodies and `Content-Length`; chunked transfer coding,
compression, cookies, redirects, proxies, authentication, multipart forms,
IPv6, and persistent connections are deferred.

HTTPS is a separate milestone. It requires TLS, certificate validation,
trusted roots, secure randomness, and wall-clock time. `https://` must be
rejected explicitly until those contracts exist.

## Consequences

The kernel gains only a reusable datagram primitive and remains unaware of DNS,
URLs, HTTP methods, headers, and domain names. Protocol parsing can be tested
on the host without booting Arwill. The first `curl` user experience is less
compact than a conventional multi-argument command, but it preserves the
existing public launch contract until a general argument model has its own
consumer and ADR.

The initial client is intentionally small and interoperable rather than
POSIX-compatible or feature-complete.

The UDP increment uses four fixed kernel endpoints and one UDP endpoint per
AWP owner. A datagram payload is capped at 512 bytes in the kernel and at the
existing 256-byte AWP copy bound per syscall. The endpoint is connected to one
IPv4/port tuple before sending or receiving; this keeps source validation and
the user ABI bounded.

The added `int 0x80` calls use the existing signed result convention:

| `rax` | operation | arguments |
| --- | --- | --- |
| 16 | `udp_open` | none |
| 17 | `udp_bind` | `rdi=local_port` |
| 18 | `udp_connect` | `rdi=IPv4 big-endian value`, `rsi=remote_port` |
| 19 | `udp_send` | `rdi=buffer`, `rsi=length` |
| 20 | `udp_receive` | `rdi=buffer`, `rsi=capacity` |
| 21 | `udp_close` | none |

`udp_connect` is asynchronous while ARP resolves. `udp_receive` returns one
complete datagram and never a partial datagram. Retry, invalid, unavailable,
and address-in-use reuse the result values from ADR-0061. Exit, cancellation,
fault, and AWP slot reuse release the owned UDP endpoint.

## Verification

Native tests cover URL validation, default and explicit ports, GET and POST
wire formatting, DNS query encoding, compressed DNS names, CNAME skipping,
truncated responses, identifier mismatches, and missing A records.

The UDP increment must add native packet tests. The completed application must
be verified in QEMU against a deterministic DNS and HTTP fixture for both GET
and POST while the existing remote console and stored applications continue
to work.

The `0.24.0` implementation was additionally verified end-to-end in QEMU with
`GET http://example.com/` and `POST http://httpbin.org/post`; both resolved
through the user-space DNS client and returned the HTTP body. These live checks
prove interoperability but do not replace the still-planned deterministic
local fixture.

## Revisit

Revisit a general launch-argument model when at least two applications need
non-path arguments. Revisit TLS only after entropy, time, certificate storage,
and cryptographic-library boundaries are explicitly designed.
