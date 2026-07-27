#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: smoke_https_qemu.sh <qemu> <machine> <disk> <log> <exit-status>" >&2
    exit 2
fi

qemu=$1
machine=$2
disk_image=$3
serial_log=$4
expected_status=$5
input_fifo=$serial_log.input
status_log=$serial_log.status

rm -f "$serial_log" "$input_fifo" "$status_log"
mkfifo "$input_fifo"
exec 3<>"$input_fifo"

(
    set +e
    "$qemu" -M "$machine" -cpu max -m 128M -boot c \
        -serial stdio -monitor none -display none -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -drive file="$disk_image",format=raw,if=ide,index=0,media=disk \
        -netdev user,id=net0 \
        -device e1000,netdev=net0,mac=52:54:00:12:34:56 \
        < "$input_fifo" > "$serial_log" 2>&1
    printf '%s\n' "$?" > "$status_log"
) &
qemu_pid=$!

cleanup() {
    exec 3>&-
    if kill -0 "$qemu_pid" >/dev/null 2>&1; then
        kill "$qemu_pid" >/dev/null 2>&1 || true
    fi
    wait "$qemu_pid" >/dev/null 2>&1 || true
    rm -f "$input_fifo"
}
trap cleanup EXIT INT TERM

wait_for_log() {
    pattern=$1
    count=0
    while [ "$count" -lt 1200 ]; do
        if grep -F -q "$pattern" "$serial_log" 2>/dev/null; then
            return 0
        fi
        if ! kill -0 "$qemu_pid" >/dev/null 2>&1; then
            echo "QEMU exited while waiting for: $pattern" >&2
            return 1
        fi
        sleep 0.1
        count=$((count + 1))
    done
    echo "timeout waiting for: $pattern" >&2
    return 1
}

wait_for_count() {
    pattern=$1
    expected=$2
    count=0
    while [ "$count" -lt 1200 ]; do
        actual=$(grep -F -c "$pattern" "$serial_log" 2>/dev/null || true)
        if [ "$actual" -ge "$expected" ]; then
            return 0
        fi
        if ! kill -0 "$qemu_pid" >/dev/null 2>&1; then
            echo "QEMU exited while waiting for: $pattern" >&2
            return 1
        fi
        sleep 0.1
        count=$((count + 1))
    done
    echo "timeout waiting for occurrence $expected of: $pattern" >&2
    return 1
}

wait_for_log "Arwill:/> "
printf 'exec curl\r' >&3
wait_for_log "method [GET/POST]: "
printf 'GET\r' >&3
wait_for_log "url: "
printf 'https://example.com/\r' >&3
wait_for_log "Example Domain"
wait_for_count "exec: exited 0" 1

printf 'exec curl\r' >&3
wait_for_count "method [GET/POST]: " 2
printf 'POST\r' >&3
wait_for_count "url: " 2
printf 'https://httpbin.org/post\r' >&3
wait_for_log "body: "
printf 'arwill-https-post\r' >&3
wait_for_log '"data": "arwill-https-post"'
wait_for_count "exec: exited 0" 2

printf 'exit\r' >&3
wait "$qemu_pid" || true
qemu_status=$(cat "$status_log")
if [ "$qemu_status" -ne "$expected_status" ]; then
    echo "unexpected QEMU exit status: $qemu_status" >&2
    exit 1
fi

echo "QEMU live HTTPS GET/POST smoke test passed"
