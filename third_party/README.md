# Third-Party Dependencies

Third-party code is not Arwill-owned source code.

`make setup` fetches Limine v12.4.2 into `third_party/limine/` from the
official GitHub release, verifies the downloaded archives by SHA-256, and
builds the Limine host tool locally.

Fetched Limine files are ignored by git. They are external boot
infrastructure used to load the Arwill kernel in QEMU.

Limine is licensed separately. The setup script copies the Limine protocol
license into `third_party/limine/LIMINE-PROTOCOL-LICENSE` and leaves the
binary package license in `third_party/limine/LICENSE`.
