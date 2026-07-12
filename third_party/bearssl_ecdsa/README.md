# BearSSL ECDSA P-256 signing subset

This directory contains the BearSSL 0.6 SHA-256 RFC 6979 ECDSA signing path,
P-256 curve constants, HMAC-DRBG, and only the i31 helpers reached by signing.
The full BearSSL TLS, X.509, RSA, and public-header surface is not imported.

Upstream archive SHA-256:
`6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`.

Upstream files retain their MIT notices; trailing spaces were normalized. The
signing source was reduced only by removing unreachable P-384 and P-521 switch
cases. `inner.h` and the adapter are Arwill compatibility glue.
