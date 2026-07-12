#!/bin/sh
set -eu

if [ "$#" -ne 6 ]; then
    echo "usage: smoke_qemu.sh <qemu-system-x86_64> <machine> <image.iso> <test-disk> <serial-log> <poweroff-exit-status>" >&2
    exit 2
fi

qemu=$1
machine=$2
iso=$3
test_disk=$4
serial_log=$5
expected_qemu_status=$6
qemu_status_log=$serial_log.status
reboot_serial_log=$serial_log.reboot
reboot_status_log=$serial_log.reboot.status

rm -f "$serial_log" "$qemu_status_log" "$reboot_serial_log" "$reboot_status_log"

(
    set +e

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
    printf '\321\200\321\203\320\264\t\r'
    wait_for_log "Tab        complete"
    sleep 0.1
    printf 'ver\t\r'
    wait_for_log_count "Arwill 0.6.0" 2
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
    printf 'blk\t\r'
    wait_for_log "sample: ARWILL-BLOCK-DEVICE-TEST"
    sleep 0.1
    printf 'irqi\t\r'
    wait_for_log "timer observed: yes"
    sleep 0.1
    printf 'irqp\t\r'
    wait_for_log "exception probe: handled vector 3"
    sleep 0.1
    printf 'sched\t\r'
    wait_for_log "scheduler: timer tick round-robin foundation"
    sleep 0.1
    printf 'run he\t\r'
    wait_for_log "process hello: hello from pid"
    sleep 0.1
    printf 'run userh\t\r'
    wait_for_log "user hello: hello from ring 3"
    sleep 0.1
    printf 'run userb\t\r'
    wait_for_log "run: spawned pid 3: userbad"
    sleep 0.1
    printf 'useri\t\r'
    wait_for_log "bad syscalls: 1"
    sleep 0.1
    printf 'owneri\t\r'
    wait_for_log "owner model: single-owner"
    sleep 0.1
    printf 'ps\r'
    wait_for_log "pid state runs exit name"
    wait_for_log "finished"
    sleep 0.1
    printf 'l\t\r'
    wait_for_log "system/"
    sleep 0.1
    printf 'write /owner/note owner note persisted across reboot\r'
    wait_for_log "write: wrote 34 bytes to /owner/note"
    sleep 0.1
    printf 'cat /owner/note\r'
    wait_for_log "owner note persisted across reboot"
    sleep 0.1
    printf '\321\201\320\262 .\320\270\t\r'
    wait_for_log "Arwill:/boot> "
    sleep 0.1
    printf 'pwd\r'
    wait_for_log_count "Arwill:/boot> " 2
    sleep 0.1
    printf 'l\t\r'
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
    wait_for_log "version: 0.6.0"
    sleep 0.1
    printf 'stat /system/i\t\r'
    wait_for_log "type: text file"
    sleep 0.1
    printf 'cat /docs/readme\r'
    wait_for_log "storage-backed filesystem"
    sleep 0.1
    printf 'cat /docs/missing\r'
    wait_for_log "cat: no such file: /docs/missing"
    sleep 0.1
    printf 'ex\t\r'
    ) | "$qemu" -M "$machine" -m 128M -cdrom "$iso" -boot d \
        -serial stdio -monitor none -display none -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -drive file="$test_disk",format=raw,if=ide,index=0,media=disk \
        > "$serial_log" 2>&1

    qemu_status=$?
    printf '%s\n' "$qemu_status" > "$qemu_status_log"
) &
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
    if [ -f "$qemu_status_log" ]; then
        break
    fi

    sleep 0.2
    count=$((count + 1))
done

if [ ! -f "$qemu_status_log" ]; then
    echo "QEMU did not power off before timeout" >&2
    cleanup
    exit 1
fi

wait "$qemu_pid" >/dev/null 2>&1 || true
trap - EXIT INT TERM

if [ ! -f "$serial_log" ]; then
    echo "QEMU did not create serial log: $serial_log" >&2
    exit 1
fi

qemu_status=$(cat "$qemu_status_log")

if [ "$qemu_status" -ne "$expected_qemu_status" ]; then
    echo "unexpected QEMU exit status: $qemu_status, expected: $expected_qemu_status" >&2
    echo "--- serial log ---" >&2
    cat "$serial_log" >&2
    echo "------------------" >&2
    exit 1
fi

check_line() {
    expected=$1

    if ! grep -F -q "$expected" "$serial_log"; then
        echo "missing expected serial output: $expected" >&2
        echo "--- serial log ---" >&2
        cat "$serial_log" >&2
        echo "------------------" >&2
        exit 1
    fi
}

check_absent() {
    unexpected=$1

    if grep -F -q "$unexpected" "$serial_log"; then
        echo "unexpected serial output: $unexpected" >&2
        echo "--- serial log ---" >&2
        cat "$serial_log" >&2
        echo "------------------" >&2
        exit 1
    fi
}

check_line "Arwill 0.6.0"
check_line "architecture: x86_64"
check_line "platform: qemu"
check_line "console: serial"
check_line "input: serial"
check_line "owner: single-owner"
check_line "shell: ready"
check_line "filesystem: arfs writable owner note"
check_line "block: qemu ata pio"
check_line "memory: boot memory map"
check_line "allocator: physical page bump allocator"
check_line "processes: kernel cooperative"
check_line "interrupts: x86_64 idt pic pit"
check_line "scheduler: timer tick foundation"
check_line "user: x86_64 ring3 int80"
check_line "power: qemu debug exit"
check_line "status: kernel initialized"
check_line "commands:"
check_line "Arwill:/> help"
check_line "Arwill 0.6.0"
check_line "Tab        complete"
check_line "clear      clear the terminal screen"
check_line "ls [path]  list the current filesystem"
check_line "write [path] [text] overwrite a writable text file"
check_line "meminfo    show memory map and page allocator"
check_line "blkinfo    show block device read diagnostics"
check_line "irqinfo    show interrupt and timer diagnostics"
check_line "irqprobe   trigger a safe breakpoint exception"
check_line "schedinfo  show scheduler tick diagnostics"
check_line "userinfo   show user-mode diagnostics"
check_line "ownerinfo  show the OS ownership model"
check_line "ps         show kernel process table"
check_line "run [name] launch a built-in kernel process"
check_line "Up/Down    browse command history"
check_absent "dir [path]"
check_absent "info [path]"
check_absent "poweroff"
check_line "memory map:"
check_line "usable"
check_line "physical allocator:"
check_line "page size: 4096 bytes"
check_line "block device: qemu ata pio"
check_line "sector size: 512 bytes"
check_line "sample lba: 1"
check_line "sample: ARWILL-BLOCK-DEVICE-TEST"
check_line "idt: loaded"
check_line "pic: remapped"
check_line "timer: configured"
check_line "enabled: yes"
check_line "timer observed: yes"
check_line "timer ticks:"
check_line "exception probe: handled vector 3"
check_line "scheduler: timer tick round-robin foundation"
check_line "scheduler ticks:"
check_line "scheduler slots: 2"
check_line "slot shell ticks:"
check_line "slot idle ticks:"
check_line "run: spawned pid"
check_line "process hello: hello from pid"
check_line "run: spawned pid 2: userhello"
check_line "user hello: hello from ring 3"
check_line "run: spawned pid 3: userbad"
check_line "user: x86_64 ring3 int80"
check_line "available: yes"
check_line "hhdm: yes"
check_line "gdt: loaded"
check_line "tss: loaded"
check_line "syscall gate: loaded"
check_line "runs: 2"
check_line "bad syscalls: 1"
check_line "owner model: single-owner"
check_line "accounts: none"
check_line "owner access: full system control"
check_line "kernel boundary: ring 3 programs use syscalls"
check_line "privileged code: explicit kernel or driver work"
check_line "pid state runs exit name"
check_line "2 finished 1 7 userhello"
check_line "3 finished 1 127 userbad"
check_line "finished"
check_line "boot/"
check_line "docs/"
check_line "owner/"
check_line "system/"
check_line "write: wrote 34 bytes to /owner/note"
check_line "owner note persisted across reboot"
check_line "Arwill:/> cd /boot/"
check_line "/boot"
check_line "kernel.elf"
check_line "limine/"
check_line "limine.conf"
check_line "protocol: limine"
check_line "cat: cannot display binary file: /boot/kernel.elf"
check_line "name: Arwill"
check_line "version: 0.6.0"
check_line "filesystem: arfs"
check_line "type: text file"
check_line "Arwill storage-backed filesystem"
check_line "writable: /owner/note"
check_line "cat: no such file: /docs/missing"
check_line "Arwill:/boot/limine> "
check_line "Arwill:/boot> exit"
check_line "status: powering off"

(
    set +e

    (
    wait_for_reboot_log() {
        pattern=$1
        count=0

        while [ "$count" -lt 100 ]; do
            if [ -f "$reboot_serial_log" ] && grep -q "$pattern" "$reboot_serial_log"; then
                return 0
            fi

            sleep 0.1
            count=$((count + 1))
        done

        return 1
    }

    wait_for_reboot_log "Arwill:/> "
    sleep 0.1
    printf 'cat /owner/note\r'
    wait_for_reboot_log "owner note persisted across reboot"
    sleep 0.1
    printf 'ex\t\r'
    ) | "$qemu" -M "$machine" -m 128M -cdrom "$iso" -boot d \
        -serial stdio -monitor none -display none -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -drive file="$test_disk",format=raw,if=ide,index=0,media=disk \
        > "$reboot_serial_log" 2>&1

    printf '%s\n' "$?" > "$reboot_status_log"
) &
reboot_qemu_pid=$!

cleanup_reboot() {
    if kill -0 "$reboot_qemu_pid" >/dev/null 2>&1; then
        kill "$reboot_qemu_pid" >/dev/null 2>&1 || true
        wait "$reboot_qemu_pid" >/dev/null 2>&1 || true
    fi
}

trap cleanup_reboot EXIT INT TERM

count=0

while [ "$count" -lt "$deadline" ]; do
    if [ -f "$reboot_status_log" ]; then
        break
    fi

    sleep 0.2
    count=$((count + 1))
done

if [ ! -f "$reboot_status_log" ]; then
    echo "QEMU reboot persistence check did not power off before timeout" >&2
    cleanup_reboot
    exit 1
fi

wait "$reboot_qemu_pid" >/dev/null 2>&1 || true
trap - EXIT INT TERM

reboot_qemu_status=$(cat "$reboot_status_log")

if [ "$reboot_qemu_status" -ne "$expected_qemu_status" ]; then
    echo "unexpected reboot QEMU exit status: $reboot_qemu_status, expected: $expected_qemu_status" >&2
    echo "--- reboot serial log ---" >&2
    cat "$reboot_serial_log" >&2
    echo "-------------------------" >&2
    exit 1
fi

if ! grep -F -q "owner note persisted across reboot" "$reboot_serial_log"; then
    echo "missing reboot persistence output" >&2
    echo "--- reboot serial log ---" >&2
    cat "$reboot_serial_log" >&2
    echo "-------------------------" >&2
    exit 1
fi

echo "QEMU serial smoke test passed"
cat "$serial_log"
cat "$reboot_serial_log"
