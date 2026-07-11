#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: check_artifacts.sh <kernel.elf> <image.iso>" >&2
    exit 2
fi

kernel=$1
iso=$2

if [ ! -s "$kernel" ]; then
    echo "missing or empty kernel artifact: $kernel" >&2
    exit 1
fi

if [ ! -s "$iso" ]; then
    echo "missing or empty boot artifact: $iso" >&2
    exit 1
fi

case "$(file "$kernel")" in
    *"ELF 64-bit"*)
        ;;
    *)
        echo "kernel artifact is not an ELF64 executable: $kernel" >&2
        file "$kernel" >&2
        exit 1
        ;;
esac

case "$(file "$iso")" in
    *"ISO 9660"*)
        ;;
    *)
        echo "boot artifact is not an ISO 9660 image: $iso" >&2
        file "$iso" >&2
        exit 1
        ;;
esac

echo "artifact check passed"
