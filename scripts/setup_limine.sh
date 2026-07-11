#!/bin/sh
set -eu

version=12.4.2
tag=v12.4.2

binary_url="https://github.com/Limine-Bootloader/Limine/releases/download/${tag}/limine-binary.tar.xz"
binary_sha256="b124cde69539a4b00d05493e1066fabd0f6a23be65fda128afe8d4243831d2f2"

source_url="https://github.com/Limine-Bootloader/Limine/releases/download/${tag}/limine-${version}.tar.xz"
source_sha256="f1ac8d3ba0c4076302a8e3c4f70a7ecee680d7a9be660c84fdbcf631a772b2c0"

cache_dir="third_party/.cache"
dest_dir="third_party/limine"
binary_archive="${cache_dir}/limine-binary-${version}.tar.xz"
source_archive="${cache_dir}/limine-source-${version}.tar.xz"

download() {
    url=$1
    output=$2

    if [ ! -f "$output" ]; then
        echo "fetching $url"
        curl -L "$url" -o "$output"
    fi
}

verify_sha256() {
    expected=$1
    file=$2

    printf "%s  %s\n" "$expected" "$file" | shasum -a 256 -c -
}

mkdir -p "$cache_dir"

download "$binary_url" "$binary_archive"
verify_sha256 "$binary_sha256" "$binary_archive"

download "$source_url" "$source_archive"
verify_sha256 "$source_sha256" "$source_archive"

if [ -x "${dest_dir}/limine" ] && [ -f "${dest_dir}/limine.h" ]; then
    exit 0
fi

tmp_dir="${cache_dir}/limine-extract.$$"
rm -rf "$tmp_dir"
mkdir -p "$tmp_dir"

tar -xJf "$binary_archive" -C "$tmp_dir"
tar -xJf "$source_archive" -C "$tmp_dir"

rm -rf "$dest_dir"
mkdir -p "$dest_dir"

cp -R "${tmp_dir}/limine-binary/." "$dest_dir/"
cp "${tmp_dir}/limine-${version}/limine-protocol/include/limine.h" "$dest_dir/limine.h"
cp "${tmp_dir}/limine-${version}/limine-protocol/LICENSE" "$dest_dir/LIMINE-PROTOCOL-LICENSE"
cp "${tmp_dir}/limine-${version}/limine-protocol/PROTOCOL.md" "$dest_dir/LIMINE-PROTOCOL.md"

make -C "$dest_dir"

rm -rf "$tmp_dir"
