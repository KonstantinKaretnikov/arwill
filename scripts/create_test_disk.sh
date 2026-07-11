#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: create_test_disk.sh <output-image>" >&2
    exit 2
fi

output=$1
temporary=$output.tmp

mkdir -p "$(dirname "$output")"
rm -f "$temporary"

dd if=/dev/zero of="$temporary" bs=512 count=16 >/dev/null 2>&1
printf 'ARWILL-BLOCK-DEVICE-TEST\n' |
    dd of="$temporary" bs=512 seek=1 conv=notrunc >/dev/null 2>&1
printf 'lba=2\nsector-size=512\n' |
    dd of="$temporary" bs=512 seek=2 conv=notrunc >/dev/null 2>&1

mv "$temporary" "$output"
