#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: create_test_disk.sh <output-image> <project-version> <hello-awp> <calc-awp> <edit-awp>" >&2
    exit 2
fi

output=$1
project_version=$2
hello_app=$3
calc_app=$4
edit_app=$5
temporary=$output.tmp
payload_dir=$output.payloads

mkdir -p "$(dirname "$output")"
rm -f "$temporary" "$payload_dir"/*
mkdir -p "$payload_dir"

dd if=/dev/zero of="$temporary" bs=512 count=96 >/dev/null 2>&1
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
arwill_config=$payload_dir/arwill.conf
app_hello=$payload_dir/hello.awp
app_calc=$payload_dir/calc.awp
app_edit=$payload_dir/edit.awp

printf 'name: Arwill\nversion: %s\narchitecture: x86_64\nplatform: qemu\nfilesystem: arfs\n' \
    "$project_version" > "$identity"

printf 'timeout: 0\nquiet: yes\nserial: yes\n\n/Arwill\nprotocol: limine\npath: boot():/boot/kernel.elf\n' \
    > "$limine_conf"

printf 'Arwill storage-backed filesystem\nsource: bootable system disk ARFS region\nwritable: ARFS v2 mutable core\n' \
    > "$readme"

printf 'owner note: empty\n' > "$owner_note"
printf 'config.version=1\nremote.enabled=true\nremote.port=23232\nremote.key=arwill\nlog.level=info\n' \
    > "$arwill_config"

identity_size=$(wc -c < "$identity" | tr -d ' ')
limine_conf_size=$(wc -c < "$limine_conf" | tr -d ' ')
readme_size=$(wc -c < "$readme" | tr -d ' ')
owner_note_size=$(wc -c < "$owner_note" | tr -d ' ')
arwill_config_size=$(wc -c < "$arwill_config" | tr -d ' ')

cp "$hello_app" "$app_hello"
app_hello_size=$(wc -c < "$app_hello" | tr -d ' ')
cp "$calc_app" "$app_calc"
app_calc_size=$(wc -c < "$app_calc" | tr -d ' ')
cp "$edit_app" "$app_edit"
app_edit_size=$(wc -c < "$app_edit" | tr -d ' ')

if [ "$app_hello_size" -gt 512 ] || [ "$app_calc_size" -gt 2048 ] ||
   [ "$app_edit_size" -gt 8192 ]; then
    echo "test application exceeds its ARFS slot" >&2
    exit 1
fi

printf 'D /apps\nD /boot\nD /boot/limine\nD /docs\nD /owner\nD /system\nF /apps/hello.awp binary 13 %s\nF /apps/calc.awp binary 14 %s\nF /apps/edit.awp binary 18 %s\nF /boot/kernel.elf binary 0 0\nF /boot/limine/limine.conf text 10 %s\nF /docs/readme text 11 %s\nF /owner/arwill.conf text 34 %s\nF /owner/note text 12 %s\nF /system/identity text 8 %s\n' \
    "$app_hello_size" "$app_calc_size" "$app_edit_size" "$limine_conf_size" "$readme_size" "$arwill_config_size" "$owner_note_size" "$identity_size" > "$manifest"

printf 'ARFS2\nmanifest_lba=4\nmanifest_sectors=2\ndata_lba=14\n' > "$superblock"

dd if="$superblock" of="$temporary" bs=512 seek=3 conv=notrunc >/dev/null 2>&1
dd if="$manifest" of="$temporary" bs=512 seek=4 conv=notrunc >/dev/null 2>&1
dd if="$identity" of="$temporary" bs=512 seek=8 conv=notrunc >/dev/null 2>&1
dd if="$limine_conf" of="$temporary" bs=512 seek=10 conv=notrunc >/dev/null 2>&1
dd if="$readme" of="$temporary" bs=512 seek=11 conv=notrunc >/dev/null 2>&1
dd if="$owner_note" of="$temporary" bs=512 seek=12 conv=notrunc >/dev/null 2>&1
dd if="$app_hello" of="$temporary" bs=512 seek=13 conv=notrunc >/dev/null 2>&1
dd if="$app_calc" of="$temporary" bs=512 seek=14 conv=notrunc >/dev/null 2>&1
dd if="$app_edit" of="$temporary" bs=512 seek=18 conv=notrunc >/dev/null 2>&1
dd if="$arwill_config" of="$temporary" bs=512 seek=34 conv=notrunc >/dev/null 2>&1

mv "$temporary" "$output"
