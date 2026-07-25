#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: build.sh <output-awp>" >&2
    exit 2
fi

output=$1
temporary=$output.tmp
llvm_prefix=$(brew --prefix llvm 2>/dev/null || true)
lld_prefix=$(brew --prefix lld 2>/dev/null || true)
clang=${CLANG:-clang}
ld=${LD_LLD:-ld.lld}
objcopy=${OBJCOPY:-llvm-objcopy}
if [ -n "$llvm_prefix" ]; then
    clang=${CLANG:-$llvm_prefix/bin/clang}
    ld=${LD_LLD:-${lld_prefix:+$lld_prefix/bin/ld.lld}}
    objcopy=${OBJCOPY:-$llvm_prefix/bin/llvm-objcopy}
fi
mkdir -p "$(dirname "$output")"
rm -f "$temporary" "$temporary.start.o" "$temporary.curl.o" \
    "$temporary.http.o" "$temporary.dns.o" "$temporary.elf" "$temporary.bin"

common_flags="--target=x86_64-elf -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check -fpie -fno-plt -mno-red-zone -mcmodel=small -Os -Wall -Wextra -Werror -Wpedantic -Iinclude"
"$clang" $common_flags -c apps/curl/curl.c -o "$temporary.curl.o"
"$clang" $common_flags -c libs/libhttp/http.c -o "$temporary.http.o"
"$clang" $common_flags -c libs/libnet/dns.c -o "$temporary.dns.o"
"$clang" --target=x86_64-elf -c apps/curl/start.S -o "$temporary.start.o"
"$ld" -pie -nostdlib -T apps/curl/linker.ld -e _start \
    -o "$temporary.elf" "$temporary.start.o" "$temporary.curl.o" \
    "$temporary.http.o" "$temporary.dns.o"
"$objcopy" -O binary "$temporary.elf" "$temporary.bin"

code_size=$(wc -c < "$temporary.bin" | tr -d ' ')
if [ "$code_size" -gt 8176 ]; then
    echo "curl exceeds two AWP code pages" >&2
    exit 1
fi

dd if=/dev/zero of="$temporary" bs=1 count=$((16 + code_size)) >/dev/null 2>&1
printf '\101\127\120\061\020\000\000\000' |
    dd of="$temporary" bs=1 seek=0 conv=notrunc >/dev/null 2>&1
byte0=$(printf '%03o' $((code_size & 255)))
byte1=$(printf '%03o' $(((code_size >> 8) & 255)))
byte2=$(printf '%03o' $(((code_size >> 16) & 255)))
byte3=$(printf '%03o' $(((code_size >> 24) & 255)))
printf "\\$byte0\\$byte1\\$byte2\\$byte3" |
    dd of="$temporary" bs=1 seek=8 conv=notrunc >/dev/null 2>&1
dd if="$temporary.bin" of="$temporary" bs=1 seek=16 conv=notrunc >/dev/null 2>&1
mv "$temporary" "$output"
rm -f "$temporary.start.o" "$temporary.curl.o" "$temporary.http.o" \
    "$temporary.dns.o" "$temporary.elf" "$temporary.bin"
