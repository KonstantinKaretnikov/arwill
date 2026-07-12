# BearSSL SHA-256 subset

Arwill vendors only three C source files from BearSSL 0.6:

- `src/hash/sha2small.c`
- `src/codec/dec32be.c`
- `src/codec/enc32be.c`

Upstream: <https://bearssl.org/>

Archive: `bearssl-0.6.tar.gz`

Archive SHA-256:
`6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`

`inner.h` is a small Arwill compatibility header containing only the types,
descriptors, endian helpers, and freestanding memory helpers required by those
files. No TLS, X.509, or unused BearSSL public API is imported.

The only source change is removal of trailing spaces. The upstream license is
retained as `LICENSE.txt`, and each imported source file retains its original
copyright and license notice.
