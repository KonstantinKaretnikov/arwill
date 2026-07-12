# ADR-0027: Arwill Program Image v1

Status: Superseded by ADR-0028

## Context

Arwill can already enter ring 3 for tiny built-in user programs. The next step
is to run code loaded from storage, but a full ELF loader would add format,
relocation, linker, argument, and process-model complexity before the project
needs it.

Arwill is intentionally a simple single-owner OS for boards and experiments, so
the first stored executable format should be small and inspectable.

## Decision

Define Arwill Program Image v1 (`API1`) as the first stored executable format.
The first header contains:

- magic `API1`;
- header size;
- entry offset;
- code size;
- reserved bytes.

The shell command `exec [path]` reads a binary file through the filesystem
contract and asks the user runtime to map and execute its code bytes in ring 3.
The first implementation supports one user code page and the existing `int
0x80` syscall ABI.

The test disk includes `/programs/hello.api`, which writes
`api hello from storage` through syscall `write` and exits with code `9`.

## Consequences

Arwill can now execute a stored program image without adopting ELF or POSIX
semantics. This gives the owner a small path for experiments while preserving a
clear future boundary.

This does not add ELF loading, dynamic linking, argument passing, environment
variables, file descriptors, per-process address spaces, or a general program
toolchain.
