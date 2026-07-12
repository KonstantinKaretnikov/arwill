# BearSSL X25519 subset

Arwill vendors two C source files from BearSSL 0.6:

- `src/ec/ec_c25519_m31.c`
- `src/codec/ccopy.c`

Upstream: <https://bearssl.org/>

Archive: `bearssl-0.6.tar.gz`

Archive SHA-256:
`6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`

The only source change is removal of trailing spaces. `inner.h` is an
Arwill-owned compatibility header containing only the EC vtable, constant-time
helpers, and freestanding memory helpers used by these two files. The small
adapter exposes only point multiplication to the kernel. The full BearSSL
public and internal headers are not imported.

The upstream MIT license is retained as `LICENSE.txt`, and both imported files
retain their copyright and license notices.
