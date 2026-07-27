# BearSSL in Arwill

Arwill vendors BearSSL 0.6 from the official upstream archive:

- source: `https://bearssl.org/bearssl-0.6.tar.gz`
- SHA-256: `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`
- license: MIT; see `LICENSE.txt`

Arwill vendors the public headers and only the upstream source files extracted
by the linker for the current TLS client profile. The retained upstream files
are unmodified; unused servers, tools, cryptographic implementations, and T0
generator inputs are omitted so the dependency remains reviewable.

`apps/curl/build.sh` can reproduce the linker extraction trace by setting
`ARWILL_BEARSSL_WHY_EXTRACT` to an output path. Arwill disables BearSSL host OS
entropy, host time, x86 AES-NI, and SSE2 auto-detection. Entropy and
certificate-validation time are injected through the AWP ABI.
