# ADR-0051: Filesystem Storage Statistics

Status: accepted

## Context

ARFS already has bounded mutable storage, but the owner cannot inspect its
remaining entry capacity, allocated sectors, or fragmentation without looking
at the disk image outside Arwill. The existing block-device contract knows raw
device geometry but does not own filesystem allocation policy. Putting ARFS
details directly into the shell would also create an unnecessary dependency
from shell orchestration to one filesystem implementation.

## Decision

Release filesystem storage statistics as Arwill `0.17.1`.

Add one optional snapshot operation to the architecture-independent filesystem
contract. A successful snapshot reports entry use and capacity, manifest and
data allocation-unit counts, the largest contiguous free run, and current path
and file-size limits. Filesystems that cannot provide these values leave the
operation unsupported; `system storage` reports `unavailable` in that case.

ARFS computes the snapshot from its mounted manifest and fixed data area. It
counts each data sector once, even if malformed metadata contains overlapping
ranges, and reports its existing 24-entry, 63-byte path, and 8192-byte file
limits. The shell exposes the snapshot only as `system storage`, with fixed Tab
completion consistent with ADR-0050.

Do not add byte-level free-space promises, quotas, reservations, partition
inspection, filesystem repair, allocation maps, or a generic disk-capacity API
in this milestone. The reported free sectors describe current ARFS allocation
metadata; they do not add journaling, atomic metadata updates, or crash
consistency.

## Consequences

The owner can see whether ARFS is approaching its entry or contiguous-storage
limits before a write fails. The shell remains independent of ARFS internals,
and the static boot catalog can honestly report that statistics are
unavailable.

The snapshot scans the small fixed ARFS data area when requested. This bounded
cost is appropriate for the current test disk but is not a design for a large
filesystem.

## Verification

Build Arwill and run native tests plus the QEMU serial/TCP smoke path. Verify
`system storage` completion and the fixed limits, observe entry use change from
15/24 to 18/24 after creating a directory and two files, observe 18/24 after
reboot, then observe 15/24 after cleanup.
