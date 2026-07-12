#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: build.sh <output-awp>" >&2
    exit 2
fi

output=$1
temporary=$output.tmp

mkdir -p "$(dirname "$output")"
rm -f "$temporary"

dd if=/dev/zero of="$temporary" bs=1 count=295 >/dev/null 2>&1
printf '\101\127\120\061\020\000\000\000\027\001\000\000\000\000\000\000' |
    dd of="$temporary" bs=1 seek=0 conv=notrunc >/dev/null 2>&1
printf '\270\001\000\000\000\110\277\000\001\000\000\200\000\000\000\110\307\306\027\000\000\000\315\200\270\002\000\000\000\110\307\307\011\000\000\000\315\200\017\013' |
    dd of="$temporary" bs=1 seek=16 conv=notrunc >/dev/null 2>&1
printf 'awp hello from storage\n' |
    dd of="$temporary" bs=1 seek=272 conv=notrunc >/dev/null 2>&1

mv "$temporary" "$output"
