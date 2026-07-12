#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: create_test_disk.sh <output-image> <project-version> <hello-awp>" >&2
    exit 2
fi

output=$1
project_version=$2
hello_app=$3
temporary=$output.tmp
payload_dir=$output.payloads

mkdir -p "$(dirname "$output")"
rm -f "$temporary" "$payload_dir"/*
mkdir -p "$payload_dir"

dd if=/dev/zero of="$temporary" bs=512 count=64 >/dev/null 2>&1
printf 'ARWILL-BLOCK-DEVICE-TEST\n' |
    dd of="$temporary" bs=512 seek=1 conv=notrunc >/dev/null 2>&1
printf 'lba=2\nsector-size=512\n' |
    dd of="$temporary" bs=512 seek=2 conv=notrunc >/dev/null 2>&1

superblock=$payload_dir/superblock
manifest=$payload_dir/manifest
identity=$payload_dir/identity
limine_conf=$payload_dir/limine.conf
readme=$payload_dir/readme
owner_note=$payload_dir/owner-note
owner_state=$payload_dir/owner-state
app_hello=$payload_dir/hello.awp

printf 'name: Arwill\nversion: %s\narchitecture: x86_64\nplatform: qemu\nfilesystem: arfs\n' \
    "$project_version" > "$identity"

printf 'timeout: 0\nquiet: yes\nserial: yes\n\n/Arwill\nprotocol: limine\npath: boot():/boot/kernel.elf\n' \
    > "$limine_conf"

printf 'Arwill storage-backed filesystem\nsource: qemu ata pio test disk\nwritable: /owner/note\n' \
    > "$readme"

printf 'owner note: empty\n' > "$owner_note"

identity_size=$(wc -c < "$identity" | tr -d ' ')
limine_conf_size=$(wc -c < "$limine_conf" | tr -d ' ')
readme_size=$(wc -c < "$readme" | tr -d ' ')
owner_note_size=$(wc -c < "$owner_note" | tr -d ' ')

cp "$hello_app" "$app_hello"
app_hello_size=$(wc -c < "$app_hello" | tr -d ' ')

if [ "$app_hello_size" -gt 512 ]; then
    echo "hello app exceeds its single-sector ARFS slot" >&2
    exit 1
fi

printf 'owner_note_size=%s\n' "$owner_note_size" > "$owner_state"

printf 'D /apps\nD /boot\nD /boot/limine\nD /docs\nD /owner\nD /system\nF /apps/hello.awp binary 13 %s\nF /boot/kernel.elf binary 0 0\nF /boot/limine/limine.conf text 10 %s\nF /boot/limine/limine-bios.sys binary 0 0\nF /docs/readme text 11 %s\nF /owner/note text 12 0\nF /system/identity text 8 %s\n' \
    "$app_hello_size" "$limine_conf_size" "$readme_size" "$identity_size" > "$manifest"

printf 'ARFS1\nmanifest_lba=4\nmanifest_sectors=2\nwritable_state_lba=6\nowner_note_lba=12\nowner_note_capacity=512\n' > "$superblock"

dd if="$superblock" of="$temporary" bs=512 seek=3 conv=notrunc >/dev/null 2>&1
dd if="$manifest" of="$temporary" bs=512 seek=4 conv=notrunc >/dev/null 2>&1
dd if="$owner_state" of="$temporary" bs=512 seek=6 conv=notrunc >/dev/null 2>&1
dd if="$identity" of="$temporary" bs=512 seek=8 conv=notrunc >/dev/null 2>&1
dd if="$limine_conf" of="$temporary" bs=512 seek=10 conv=notrunc >/dev/null 2>&1
dd if="$readme" of="$temporary" bs=512 seek=11 conv=notrunc >/dev/null 2>&1
dd if="$owner_note" of="$temporary" bs=512 seek=12 conv=notrunc >/dev/null 2>&1
dd if="$app_hello" of="$temporary" bs=512 seek=13 conv=notrunc >/dev/null 2>&1

mv "$temporary" "$output"
