#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: create_test_disk.sh <output-image> <project-version>" >&2
    exit 2
fi

output=$1
project_version=$2
temporary=$output.tmp
payload_dir=$output.payloads

mkdir -p "$(dirname "$output")"
rm -f "$temporary" "$payload_dir"/*
mkdir -p "$payload_dir"

dd if=/dev/zero of="$temporary" bs=512 count=32 >/dev/null 2>&1
printf 'ARWILL-BLOCK-DEVICE-TEST\n' |
    dd of="$temporary" bs=512 seek=1 conv=notrunc >/dev/null 2>&1
printf 'lba=2\nsector-size=512\n' |
    dd of="$temporary" bs=512 seek=2 conv=notrunc >/dev/null 2>&1

superblock=$payload_dir/superblock
manifest=$payload_dir/manifest
identity=$payload_dir/identity
limine_conf=$payload_dir/limine.conf
readme=$payload_dir/readme

printf 'name: Arwill\nversion: %s\narchitecture: x86_64\nplatform: qemu\nfilesystem: arfs\n' \
    "$project_version" > "$identity"

printf 'timeout: 0\nquiet: yes\nserial: yes\n\n/Arwill\nprotocol: limine\npath: boot():/boot/kernel.elf\n' \
    > "$limine_conf"

printf 'Arwill storage-backed read-only filesystem\nsource: qemu ata pio test disk\n' \
    > "$readme"

identity_size=$(wc -c < "$identity" | tr -d ' ')
limine_conf_size=$(wc -c < "$limine_conf" | tr -d ' ')
readme_size=$(wc -c < "$readme" | tr -d ' ')

printf 'D /boot\nD /boot/limine\nD /docs\nD /system\nF /boot/kernel.elf binary 0 0\nF /boot/limine/limine.conf text 10 %s\nF /boot/limine/limine-bios.sys binary 0 0\nF /docs/readme text 11 %s\nF /system/identity text 8 %s\n' \
    "$limine_conf_size" "$readme_size" "$identity_size" > "$manifest"

printf 'ARFS1\nmanifest_lba=4\nmanifest_sectors=2\n' > "$superblock"

dd if="$superblock" of="$temporary" bs=512 seek=3 conv=notrunc >/dev/null 2>&1
dd if="$manifest" of="$temporary" bs=512 seek=4 conv=notrunc >/dev/null 2>&1
dd if="$identity" of="$temporary" bs=512 seek=8 conv=notrunc >/dev/null 2>&1
dd if="$limine_conf" of="$temporary" bs=512 seek=10 conv=notrunc >/dev/null 2>&1
dd if="$readme" of="$temporary" bs=512 seek=11 conv=notrunc >/dev/null 2>&1

mv "$temporary" "$output"
