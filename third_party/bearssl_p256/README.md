# BearSSL P-256 subset

Arwill vendors `src/ec/ec_p256_m31.c` from BearSSL 0.6 for specialized P-256
point operations. The only upstream source change is removal of trailing
spaces.

Upstream: <https://bearssl.org/>

Archive SHA-256:
`6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`

The adapter exposes only public-point derivation. The compatibility header
reuses the small EC types and constant-time copy helper already required by the
X25519 subset. No TLS, X.509, or general BearSSL public headers are imported.

The upstream MIT license is retained as `LICENSE.txt`, and the source retains
its original copyright and license notice.
