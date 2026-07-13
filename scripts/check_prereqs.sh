#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: check_prereqs.sh <clang> <ld.lld> <xorriso> <qemu>" >&2
    exit 2
fi

missing=0

check_tool() {
    tool_name=$1
    tool_path=$2

    if ! command -v "$tool_path" >/dev/null 2>&1; then
        echo "missing required tool: $tool_name ($tool_path)" >&2
        missing=1
    fi
}

check_tool "clang" "$1"
check_tool "ld.lld" "$2"
check_tool "xorriso" "$3"
check_tool "qemu-system-x86_64" "$4"
check_tool "curl" "curl"
check_tool "tar" "tar"
check_tool "shasum" "shasum"
check_tool "make" "make"
check_tool "nc" "nc"
check_tool "stty" "stty"

if [ "$missing" -ne 0 ]; then
    echo "install prerequisites with: brew install llvm xorriso qemu" >&2
    exit 1
fi
