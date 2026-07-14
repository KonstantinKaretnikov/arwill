#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: create_disk_image.sh <output> <boot-image> <arfs-seed> <region-lba> <region-sectors>" >&2
    exit 2
fi

output=$1
boot_image=$2
arfs_seed=$3
region_lba=$4
region_sectors=$5
sector_size=512
temporary=$output.tmp
region_offset=$((region_lba * sector_size))
boot_size=$(wc -c < "$boot_image" | tr -d ' ')
seed_size=$(wc -c < "$arfs_seed" | tr -d ' ')
region_size=$((region_sectors * sector_size))

if [ "$boot_size" -gt "$region_offset" ]; then
    echo "boot image overlaps the ARFS region" >&2
    exit 1
fi

if [ "$seed_size" -gt "$region_size" ]; then
    echo "ARFS seed exceeds its bounded region" >&2
    exit 1
fi

mkdir -p "$(dirname "$output")"
rm -f "$temporary"
dd if=/dev/zero of="$temporary" bs="$sector_size" \
    count=$((region_lba + region_sectors)) >/dev/null 2>&1
dd if="$boot_image" of="$temporary" bs="$sector_size" conv=notrunc \
    >/dev/null 2>&1
dd if="$arfs_seed" of="$temporary" bs="$sector_size" seek="$region_lba" conv=notrunc \
    >/dev/null 2>&1
mv "$temporary" "$output"
