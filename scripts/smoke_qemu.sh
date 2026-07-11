#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: smoke_qemu.sh <qemu-system-x86_64> <image.iso> <serial-log>" >&2
    exit 2
fi

qemu=$1
iso=$2
serial_log=$3

rm -f "$serial_log"

"$qemu" -M q35 -m 128M -cdrom "$iso" -boot d \
    -serial "file:${serial_log}" -display none -no-reboot -no-shutdown &
qemu_pid=$!

cleanup() {
    if kill -0 "$qemu_pid" >/dev/null 2>&1; then
        kill "$qemu_pid" >/dev/null 2>&1 || true
        wait "$qemu_pid" >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT INT TERM

deadline=50
count=0

while [ "$count" -lt "$deadline" ]; do
    if [ -f "$serial_log" ] && grep -q "status: kernel initialized" "$serial_log"; then
        break
    fi

    sleep 0.2
    count=$((count + 1))
done

cleanup
trap - EXIT INT TERM

if [ ! -f "$serial_log" ]; then
    echo "QEMU did not create serial log: $serial_log" >&2
    exit 1
fi

check_line() {
    expected=$1

    if ! grep -q "$expected" "$serial_log"; then
        echo "missing expected serial output: $expected" >&2
        echo "--- serial log ---" >&2
        cat "$serial_log" >&2
        echo "------------------" >&2
        exit 1
    fi
}

check_line "Arwill 0.0.1"
check_line "architecture: x86_64"
check_line "platform: qemu"
check_line "console: serial"
check_line "status: kernel initialized"

echo "QEMU serial smoke test passed"
cat "$serial_log"
