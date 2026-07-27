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
ar=${AR:-llvm-ar}
if [ -n "$llvm_prefix" ]; then
    clang=${CLANG:-$llvm_prefix/bin/clang}
    ld=${LD_LLD:-${lld_prefix:+$lld_prefix/bin/ld.lld}}
    objcopy=${OBJCOPY:-$llvm_prefix/bin/llvm-objcopy}
    ar=${AR:-$llvm_prefix/bin/llvm-ar}
fi
mkdir -p "$(dirname "$output")"
rm -f "$temporary" "$temporary.start.o" "$temporary.curl.o" \
    "$temporary.http.o" "$temporary.dns.o" "$temporary.tls.o" \
    "$temporary.memory.o" "$temporary.bearssl.a" "$temporary.elf" \
    "$temporary.bin"
rm -rf "$temporary.bearssl"

common_flags="--target=x86_64-elf -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -fno-plt -fno-builtin -ffunction-sections -fdata-sections -mno-red-zone -mgeneral-regs-only -mcmodel=large -Os -Wall -Wextra -Werror -Wpedantic -Ilibs/libc/include -Iinclude -Ithird_party/bearssl/inc"
"$clang" $common_flags -c apps/curl/curl.c -o "$temporary.curl.o"
"$clang" $common_flags -c libs/libhttp/http.c -o "$temporary.http.o"
"$clang" $common_flags -c libs/libnet/dns.c -o "$temporary.dns.o"
"$clang" $common_flags -c libs/libtls/tls.c -o "$temporary.tls.o"
"$clang" $common_flags -c libs/libc/memory.c -o "$temporary.memory.o"
mkdir -p "$temporary.bearssl"
bearssl_flags="$common_flags -Ithird_party/bearssl/src -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0"
for source in $(find third_party/bearssl/src -name '*.c' -type f | sort); do
    object="$temporary.bearssl/$(printf '%s' "$source" | tr '/.' '__').o"
    "$clang" $bearssl_flags -Wno-error -Wno-strict-prototypes \
        -Wno-unused-parameter -Wno-sign-conversion \
        -Wno-conversion -c "$source" -o "$object"
done
"$ar" rcs "$temporary.bearssl.a" "$temporary.bearssl"/*.o
"$clang" --target=x86_64-elf -c apps/curl/start.S -o "$temporary.start.o"
why_extract_arg=
if [ -n "${ARWILL_BEARSSL_WHY_EXTRACT:-}" ]; then
    why_extract_arg="--why-extract=$ARWILL_BEARSSL_WHY_EXTRACT"
fi
"$ld" -static -nostdlib --gc-sections -T apps/curl/linker.ld -e _start \
    $why_extract_arg \
    -o "$temporary.elf" "$temporary.start.o" "$temporary.curl.o" \
    "$temporary.http.o" "$temporary.dns.o" "$temporary.tls.o" \
    "$temporary.memory.o" "$temporary.bearssl.a"
"$objcopy" -O binary "$temporary.elf" "$temporary.bin"

code_size=$(wc -c < "$temporary.bin" | tr -d ' ')
if [ "$code_size" -gt 196592 ]; then
    echo "curl exceeds 48 AWP code pages" >&2
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
if [ "${ARWILL_KEEP_APP_ELF:-0}" = "1" ]; then
    cp "$temporary.elf" "$output.elf"
fi
rm -f "$temporary.start.o" "$temporary.curl.o" "$temporary.http.o" \
    "$temporary.dns.o" "$temporary.tls.o" "$temporary.memory.o" \
    "$temporary.bearssl.a" "$temporary.elf" "$temporary.bin"
rm -rf "$temporary.bearssl"
