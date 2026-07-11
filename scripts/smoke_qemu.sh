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

(
    wait_for_log() {
        pattern=$1
        count=0

        while [ "$count" -lt 100 ]; do
            if [ -f "$serial_log" ] && grep -q "$pattern" "$serial_log"; then
                return 0
            fi

            sleep 0.1
            count=$((count + 1))
        done

        return 1
    }

    wait_for_log_count() {
        pattern=$1
        expected_count=$2
        count=0

        while [ "$count" -lt 100 ]; do
            if [ -f "$serial_log" ]; then
                seen_count=$(grep -c "$pattern" "$serial_log" 2>/dev/null || true)
                if [ "$seen_count" -ge "$expected_count" ]; then
                    return 0
                fi
            fi

            sleep 0.1
            count=$((count + 1))
        done

        return 1
    }

    wait_for_log "Arwill:/> "
    sleep 0.1
    printf 'he\t\r'
    wait_for_log "Tab        complete"
    sleep 0.1
    printf 'ver\t\r'
    wait_for_log_count "Arwill 0.0.6" 2
    sleep 0.1
    printf 'pwd\r'
    wait_for_log_count "Arwill:/> " 4
    sleep 0.1
    printf '\033[A\r'
    wait_for_log_count "Arwill:/> pwd" 2
    sleep 0.1
    printf 'cl\t\r'
    wait_for_log_count "Arwill:/> " 6
    sleep 0.1
    printf 'mem\t\r'
    wait_for_log "physical allocator:"
    sleep 0.1
    printf 'l\t\r'
    wait_for_log "system/"
    sleep 0.1
    printf 'cd /b\t\r'
    wait_for_log "Arwill:/boot> "
    sleep 0.1
    printf 'pwd\r'
    wait_for_log_count "Arwill:/boot> " 2
    sleep 0.1
    printf 'd\t\r'
    wait_for_log "limine/"
    sleep 0.1
    printf 'cd l\t\r'
    wait_for_log "Arwill:/boot/limine> "
    sleep 0.1
    printf 'l\t\r'
    wait_for_log "limine.conf"
    sleep 0.1
    printf 'cat limine.c\t\r'
    wait_for_log "protocol: limine"
    sleep 0.1
    printf 'cd ..\r'
    wait_for_log_count "Arwill:/boot> " 4
    sleep 0.1
    printf 'cat kernel.elf\r'
    wait_for_log "cat: cannot display binary file: /boot/kernel.elf"
    sleep 0.1
    printf 'cat /system/i\t\r'
    wait_for_log "version: 0.0.6"
    sleep 0.1
    printf 'stat /system/i\t\r'
    wait_for_log "type: text file"
    sleep 0.1
    printf 'ha\t\r'
) | "$qemu" -M q35 -m 128M -cdrom "$iso" -boot d \
    -serial stdio -monitor none -display none -no-reboot -no-shutdown \
    > "$serial_log" 2>&1 &
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
    if [ -f "$serial_log" ] && grep -q "status: shell halted" "$serial_log"; then
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

check_line "Arwill 0.0.6"
check_line "architecture: x86_64"
check_line "platform: qemu"
check_line "console: serial"
check_line "input: serial"
check_line "shell: ready"
check_line "filesystem: static boot catalog"
check_line "memory: boot memory map"
check_line "allocator: physical page bump allocator"
check_line "status: kernel initialized"
check_line "commands:"
check_line "Arwill 0.0.6"
check_line "Tab        complete"
check_line "clear      clear the terminal screen"
check_line "meminfo    show memory map and page allocator"
check_line "Up/Down    browse command history"
check_line "memory map:"
check_line "usable"
check_line "physical allocator:"
check_line "page size: 4096 bytes"
check_line "boot/"
check_line "system/"
check_line "/boot"
check_line "kernel.elf"
check_line "limine/"
check_line "limine.conf"
check_line "protocol: limine"
check_line "cat: cannot display binary file: /boot/kernel.elf"
check_line "name: Arwill"
check_line "version: 0.0.6"
check_line "type: text file"
check_line "Arwill:/boot/limine> "
check_line "status: shell halted"

echo "QEMU serial smoke test passed"
cat "$serial_log"
