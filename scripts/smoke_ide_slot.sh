#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: smoke_ide_slot.sh <qemu> <machine> <disk.img> <serial-log> <poweroff-status>" >&2
    exit 2
fi

qemu=$1
machine=$2
disk_image=$3
serial_log=$4
expected_status=$5
status_log=$serial_log.status
input_fifo=$serial_log.input

rm -f "$serial_log" "$status_log" "$input_fifo"
mkfifo "$input_fifo"
exec 3<>"$input_fifo"

(
    set +e
    "$qemu" -M "$machine" -cpu max -m 128M -boot c \
        -serial stdio -monitor none -display none -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -drive file="$disk_image",format=raw,if=ide,index=2,media=disk \
        < "$input_fifo" > "$serial_log" 2>&1
    printf '%s\n' "$?" > "$status_log"
) &
qemu_pid=$!

cleanup() {
    exec 3>&-
    if kill -0 "$qemu_pid" >/dev/null 2>&1; then
        kill "$qemu_pid" >/dev/null 2>&1 || true
        wait "$qemu_pid" >/dev/null 2>&1 || true
    fi
    rm -f "$input_fifo"
}

trap cleanup EXIT INT TERM

wait_for_log() {
    pattern=$1
    count=0

    while [ "$count" -lt 150 ]; do
        if [ -f "$serial_log" ] && grep -F -q "$pattern" "$serial_log"; then
            return 0
        fi
        sleep 0.1
        count=$((count + 1))
    done

    return 1
}

if ! wait_for_log "Arwill:/> "; then
    echo "secondary-master image did not reach the Arwill shell" >&2
    cat "$serial_log" >&2
    exit 1
fi

printf 'devices\r' >&3
if ! wait_for_log "disk0 block qemu ata pio arfs region ready"; then
    echo "ATA discovery did not find the secondary-master system disk" >&2
    cat "$serial_log" >&2
    exit 1
fi

if ! wait_for_log "fs0 filesystem arfs mutable mounted"; then
    echo "ARFS did not mount from the secondary-master system disk" >&2
    cat "$serial_log" >&2
    exit 1
fi

printf 'exit\r' >&3

count=0
while [ "$count" -lt 100 ]; do
    if [ -f "$status_log" ]; then
        break
    fi
    sleep 0.1
    count=$((count + 1))
done

if [ ! -f "$status_log" ]; then
    echo "secondary-master QEMU did not power off" >&2
    exit 1
fi

wait "$qemu_pid" >/dev/null 2>&1 || true
qemu_status=$(cat "$status_log")
if [ "$qemu_status" -ne "$expected_status" ]; then
    echo "secondary-master QEMU status $qemu_status, expected $expected_status" >&2
    cat "$serial_log" >&2
    exit 1
fi

exec 3>&-
rm -f "$input_fifo"
trap - EXIT INT TERM

echo "QEMU secondary-master IDE discovery smoke test passed"
