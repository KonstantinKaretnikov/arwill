# macOS Development Setup

Arwill 0.0.1 is developed on macOS with Homebrew tools.

Install the required packages:

```sh
brew install llvm lld xorriso qemu
```

This repository uses:

- Homebrew LLVM `clang` to compile freestanding x86-64 ELF objects.
- Homebrew LLD `ld.lld` to link the kernel ELF.
- `xorriso` to create the Limine bootable ISO.
- QEMU to boot and smoke-test the ISO.

Verify the tools:

```sh
/opt/homebrew/opt/llvm/bin/clang --version
/opt/homebrew/opt/lld/bin/ld.lld --version
xorriso -version
qemu-system-x86_64 --version
make --version
curl --version
tar --version
shasum -a 256 /dev/null
```

Prepare third-party boot infrastructure:

```sh
make setup
```

Build:

```sh
make build
```

Run:

```sh
make run
```

Check:

```sh
make check
```

Clean generated artifacts:

```sh
make clean
```

`make clean` removes `build/`. It does not delete downloaded Limine files from
`third_party/limine/`; rerun setup only when the pinned dependency changes or
the directory is removed.
