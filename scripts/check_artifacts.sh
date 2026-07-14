#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: check_artifacts.sh <kernel.elf> <image.iso> <disk.img> <region-lba> <region-sectors>" >&2
    exit 2
fi

kernel=$1
iso=$2
disk=$3
region_lba=$4
region_sectors=$5

if [ ! -s "$kernel" ]; then
    echo "missing or empty kernel artifact: $kernel" >&2
    exit 1
fi

if [ ! -s "$iso" ]; then
    echo "missing or empty boot artifact: $iso" >&2
    exit 1
fi

if [ ! -s "$disk" ]; then
    echo "missing or empty disk artifact: $disk" >&2
    exit 1
fi

case "$(file "$kernel")" in
    *"ELF 64-bit"*)
        ;;
    *)
        echo "kernel artifact is not an ELF64 executable: $kernel" >&2
        file "$kernel" >&2
        exit 1
        ;;
esac

case "$(file "$iso")" in
    *"ISO 9660"*)
        ;;
    *)
        echo "boot artifact is not an ISO 9660 image: $iso" >&2
        file "$iso" >&2
        exit 1
        ;;
esac

expected_disk_size=$(((region_lba + region_sectors) * 512))
disk_size=$(wc -c < "$disk" | tr -d ' ')
if [ "$disk_size" -ne "$expected_disk_size" ]; then
    echo "disk image size differs: $disk_size, expected: $expected_disk_size" >&2
    exit 1
fi

arfs_magic=$(dd if="$disk" bs=512 skip=$((region_lba + 3)) count=1 2>/dev/null | \
    LC_ALL=C head -c 5)
if [ "$arfs_magic" != "ARFS2" ]; then
    echo "disk image has no ARFS2 superblock in its storage region" >&2
    exit 1
fi

echo "artifact check passed"
