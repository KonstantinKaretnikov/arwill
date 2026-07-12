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

wait_for_log_file() {
    log_file=$1
    pattern=$2
    count=0

    while [ "$count" -lt 100 ]; do
        if [ -f "$log_file" ] && grep -q "$pattern" "$log_file"; then
            return 0
        fi

        sleep 0.1
        count=$((count + 1))
    done

    return 1
}

wait_for_log_count_file() {
    log_file=$1
    pattern=$2
    expected_count=$3
    count=0

    while [ "$count" -lt 100 ]; do
        if [ -f "$log_file" ]; then
            seen_count=$(grep -c "$pattern" "$log_file" 2>/dev/null || true)
            if [ "$seen_count" -ge "$expected_count" ]; then
                return 0
            fi
        fi

        sleep 0.1
        count=$((count + 1))
    done

    return 1
}

wait_for_primary_log() {
    wait_for_log_file "$serial_log" "$1"
}

wait_for_primary_log_count() {
    wait_for_log_count_file "$serial_log" "$1" "$2"
}

wait_for_reboot_log() {
    wait_for_log_file "$reboot_serial_log" "$1"
}

run_qemu_to_log() {
    log_file=$1

    "$qemu" -M "$machine" -m 128M -cdrom "$iso" -boot d \
        -serial stdio -monitor none -display none -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -drive file="$test_disk",format=raw,if=ide,index=0,media=disk \
        > "$log_file" 2>&1
}

(
    set +e

    (
    wait_for_primary_log "Arwill:/> "
    sleep 0.1
    printf '\321\200\321\203\320\264\t\r'
    wait_for_primary_log "Tab        complete"
    sleep 0.1
    printf 'ver\t\r'
    wait_for_primary_log_count "Arwill 0.12.0" 2
    sleep 0.1
    printf 'pwd\r'
    wait_for_primary_log_count "Arwill:/> " 4
    sleep 0.1
    printf '\033[A\r'
    wait_for_primary_log_count "Arwill:/> pwd" 2
    sleep 0.1
    printf 'cl\t\r'
    wait_for_primary_log_count "Arwill:/> " 6
    sleep 0.1
    printf 'mem\t\r'
    wait_for_primary_log "physical allocator:"
    wait_for_primary_log "kernel heap:"
    wait_for_primary_log "  initialized: yes"
    sleep 0.1
    printf 'heap\t\r'
    wait_for_primary_log "heaptest: allocated and freed 2 blocks"
    wait_for_primary_log "heaptest: allocations 2, frees 2"
    sleep 0.1
    printf 'dev\t\r'
    wait_for_primary_log "name kind driver status"
    wait_for_primary_log "serial0 console qemu serial ready"
    wait_for_primary_log "fb0 console limine framebuffer text ready"
    wait_for_primary_log "disk0 block qemu ata pio ready"
    wait_for_primary_log "heap0 memory hhdm free-list ready"
    wait_for_primary_log "user0 user x86_64 ring3 int80 ready"
    sleep 0.1
    printf 'blk\t\r'
    wait_for_primary_log "sample: ARWILL-BLOCK-DEVICE-TEST"
    sleep 0.1
    printf 'irqi\t\r'
    wait_for_primary_log "timer observed: yes"
    sleep 0.1
    printf 'irqp\t\r'
    wait_for_primary_log "exception probe: handled vector 3"
    sleep 0.1
    printf 'sched\t\r'
    wait_for_primary_log "scheduler: timer tick round-robin foundation"
    sleep 0.1
    printf 'run he\t\r'
    wait_for_primary_log "process hello: hello from pid"
    sleep 0.1
    printf 'run count\t\r'
    wait_for_primary_log "process counter: pid 2 step 1/3"
    sleep 0.1
    printf 'ps\r'
    wait_for_primary_log "2 ready 1 0 counter"
    sleep 0.1
    printf 'ste\t\r'
    wait_for_primary_log "process counter: pid 2 step 2/3"
    wait_for_primary_log "step: ran 1 process step(s)"
    sleep 0.1
    printf 'step\r'
    wait_for_primary_log "process counter: pid 2 step 3/3"
    sleep 0.1
    printf 'run userh\t\r'
    wait_for_primary_log "user hello: hello from ring 3"
    sleep 0.1
    printf 'run userb\t\r'
    wait_for_primary_log "run: spawned pid 4: userbad"
    sleep 0.1
    printf 'exec /apps/hello.awp\r'
    wait_for_primary_log "awp hello from storage"
    wait_for_primary_log "exec: exited 9"
    sleep 0.1
    printf 'useri\t\r'
    wait_for_primary_log "runs: 3"
    wait_for_primary_log "bytes written: 53"
    wait_for_primary_log "bad syscalls: 1"
    sleep 0.1
    printf 'owneri\t\r'
    wait_for_primary_log "owner model: single-owner"
    sleep 0.1
    printf 'ps\r'
    wait_for_primary_log "pid state runs exit name"
    wait_for_primary_log "finished"
    sleep 0.1
    printf 'l\t\r'
    wait_for_primary_log "system/"
    wait_for_primary_log "apps/"
    sleep 0.1
    printf 'exec /apps/calc.awp\r'
    wait_for_primary_log "calc> "
    sleep 0.1
    printf '12*9\0107\r'
    wait_for_primary_log "84"
    sleep 0.1
    printf '5+6\r'
    wait_for_primary_log "11"
    sleep 0.1
    printf '\003'
    wait_for_primary_log "calc> \\^C"
    wait_for_primary_log "exec: exited 130"
    sleep 0.1
    printf 'cat /apps/hello.awp\r'
    wait_for_primary_log "cat: cannot display binary file: /apps/hello.awp"
    sleep 0.1
    printf 'mkdir /scratch\r'
    wait_for_primary_log "mkdir: created /scratch"
    sleep 0.1
    printf 'write /scratch/message persistent mutable text\r'
    wait_for_primary_log "write: wrote 23 bytes to /scratch/message"
    sleep 0.1
    printf 'writehex /scratch/data.bin 0001027f80feff\r'
    wait_for_primary_log "writehex: wrote 7 bytes to /scratch/data.bin"
    sleep 0.1
    printf 'write /owner/note owner note persisted across reboot\r'
    wait_for_primary_log "write: wrote 34 bytes to /owner/note"
    sleep 0.1
    printf 'cat /owner/note\r'
    wait_for_primary_log "owner note persisted across reboot"
    sleep 0.1
    printf 'stat /owner/note\r'
    wait_for_primary_log "size: 34 bytes"
    sleep 0.1
    printf '\321\201\320\262 .\320\270\t\r'
    wait_for_primary_log "Arwill:/boot> "
    sleep 0.1
    printf 'pwd\r'
    wait_for_primary_log_count "Arwill:/boot> " 2
    sleep 0.1
    printf 'l\t\r'
    wait_for_primary_log "limine/"
    sleep 0.1
    printf 'cd l\t\r'
    wait_for_primary_log "Arwill:/boot/limine> "
    sleep 0.1
    printf 'l\t\r'
    wait_for_primary_log "limine.conf"
    sleep 0.1
    printf 'cat limine.c\t\r'
    wait_for_primary_log "protocol: limine"
    sleep 0.1
    printf 'cd ..\r'
    wait_for_primary_log_count "Arwill:/boot> " 4
    sleep 0.1
    printf 'cat kernel.elf\r'
    wait_for_primary_log "cat: cannot display binary file: /boot/kernel.elf"
    sleep 0.1
    printf 'cat /system/i\t\r'
    wait_for_primary_log "version: 0.12.0"
    sleep 0.1
    printf 'stat /system/i\t\r'
    wait_for_primary_log "type: text file"
    sleep 0.1
    printf 'cat /docs/readme\r'
    wait_for_primary_log "storage-backed filesystem"
    sleep 0.1
    printf 'cat /docs/missing\r'
    wait_for_primary_log "cat: no such file: /docs/missing"
    sleep 0.1
    printf 'exit\r'
    ) | run_qemu_to_log "$serial_log"

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

check_line "Arwill 0.12.0"
check_line "architecture: x86_64"
check_line "platform: qemu"
check_line "console: serial"
check_line "input: serial"
check_line "owner: single-owner"
check_line "shell: ready"
check_line "filesystem: arfs mutable"
check_line "block: qemu ata pio"
check_line "memory: boot memory map"
check_line "allocator: physical page bump allocator + kernel heap"
check_line "devices: registry"
check_line "processes: kernel cooperative"
check_line "interrupts: x86_64 idt pic pit"
check_line "scheduler: timer tick foundation"
check_line "user: x86_64 ring3 int80"
check_line "power: qemu debug exit"
check_line "status: kernel initialized"
check_line "commands:"
check_line "Arwill:/> help"
check_line "Arwill 0.12.0"
check_line "Tab        complete"
check_line "clear      clear the terminal screen"
check_line "ls [path]  list the current filesystem"
check_line "mkdir [path] create a directory"
check_line "write [path] [text] create or overwrite a text file"
check_line "writehex [path] [hex] create or overwrite a binary file"
check_line "rm [path] remove a file or empty directory"
check_line "meminfo    show memory map and allocators"
check_line "heaptest   exercise kernel heap allocation"
check_line "devices    list detected devices"
check_line "blkinfo    show block device read diagnostics"
check_line "irqinfo    show interrupt and timer diagnostics"
check_line "irqprobe   trigger a safe breakpoint exception"
check_line "schedinfo  show scheduler tick diagnostics"
check_line "userinfo   show user-mode diagnostics"
check_line "ownerinfo  show the OS ownership model"
check_line "ps         show kernel process table"
check_line "run [name] launch a built-in kernel process"
check_line "exec [path] run a stored program image"
check_line "step       run one cooperative process step"
check_line "Up/Down    browse command history"
check_absent "  dir [path]  list the current filesystem"
check_absent "info [path]"
check_absent "poweroff"
check_line "memory map:"
check_line "usable"
check_line "physical allocator:"
check_line "page size: 4096 bytes"
check_line "kernel heap:"
check_line "initialized: yes"
check_line "size: 16384 bytes"
check_line "heaptest: allocated and freed 2 blocks"
check_line "heaptest: allocations 2, frees 2"
check_line "name kind driver status"
check_line "serial0 console qemu serial ready"
check_line "fb0 console limine framebuffer text ready"
check_line "input0 input qemu serial ready"
check_line "disk0 block qemu ata pio ready"
check_line "fs0 filesystem arfs mutable mounted"
check_line "heap0 memory hhdm free-list ready"
check_line "timer0 interrupts x86_64 idt pic pit ready"
check_line "power0 power qemu debug exit ready"
check_line "user0 user x86_64 ring3 int80 ready"
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
check_line "process counter: pid 2 step 1/3"
check_line "2 ready 1 0 counter"
check_line "process counter: pid 2 step 2/3"
check_line "step: ran 1 process step(s)"
check_line "process counter: pid 2 step 3/3"
check_line "run: spawned pid 3: userhello"
check_line "user hello: hello from ring 3"
check_line "run: spawned pid 4: userbad"
check_line "awp hello from storage"
check_line "exec: exited 9"
check_line "user: x86_64 ring3 int80"
check_line "available: yes"
check_line "hhdm: yes"
check_line "gdt: loaded"
check_line "tss: loaded"
check_line "syscall gate: loaded"
check_line "runs: 3"
check_line "bytes written: 53"
check_line "bad syscalls: 1"
check_line "owner model: single-owner"
check_line "accounts: none"
check_line "owner access: full system control"
check_line "kernel boundary: ring 3 programs use syscalls"
check_line "privileged code: explicit kernel or driver work"
check_line "pid state runs exit name"
check_line "2 finished 3 0 counter"
check_line "3 finished 1 7 userhello"
check_line "4 finished 1 127 userbad"
check_line "finished"
check_line "boot/"
check_line "docs/"
check_line "owner/"
check_line "apps/"
check_line "calc> "
check_line "84"
check_line "11"
check_line "exec: exited 130"
check_line "system/"
check_line "cat: cannot display binary file: /apps/hello.awp"
check_line "mkdir: created /scratch"
check_line "write: wrote 23 bytes to /scratch/message"
check_line "writehex: wrote 7 bytes to /scratch/data.bin"
check_line "write: wrote 34 bytes to /owner/note"
check_line "owner note persisted across reboot"
check_line "path: /owner/note"
check_line "size: 34 bytes"
check_line "Arwill:/> cd /boot/"
check_line "/boot"
check_line "kernel.elf"
check_line "limine/"
check_line "limine.conf"
check_line "protocol: limine"
check_line "cat: cannot display binary file: /boot/kernel.elf"
check_line "name: Arwill"
check_line "version: 0.12.0"
check_line "filesystem: arfs"
check_line "type: text file"
check_line "Arwill storage-backed filesystem"
check_line "writable: ARFS v2 mutable core"
check_line "cat: no such file: /docs/missing"
check_line "Arwill:/boot/limine> "
check_line "Arwill:/boot> exit"
check_line "status: powering off"

(
    set +e

    (
    wait_for_reboot_log "Arwill:/> "
    sleep 0.1
    printf 'cat /owner/note\r'
    wait_for_reboot_log "owner note persisted across reboot"
    sleep 0.1
    printf 'cat /scratch/message\r'
    wait_for_reboot_log "persistent mutable text"
    sleep 0.1
    printf 'stat /scratch/data.bin\r'
    wait_for_reboot_log "size: 7 bytes"
    sleep 0.1
    printf 'cat /scratch/data.bin\r'
    wait_for_reboot_log "cat: cannot display binary file: /scratch/data.bin"
    sleep 0.1
    printf 'exec /apps/hello.awp\r'
    wait_for_reboot_log "awp hello from storage"
    wait_for_reboot_log "exec: exited 9"
    sleep 0.1
    printf 'rm /scratch\r'
    wait_for_reboot_log "rm: cannot remove: /scratch"
    sleep 0.1
    printf 'rm /scratch/message\r'
    wait_for_reboot_log "rm: removed /scratch/message"
    sleep 0.1
    printf 'rm /scratch/data.bin\r'
    wait_for_reboot_log "rm: removed /scratch/data.bin"
    sleep 0.1
    printf 'write /scratch/reused reused sector\r'
    wait_for_reboot_log "write: wrote 13 bytes to /scratch/reused"
    sleep 0.1
    printf 'cat /scratch/reused\r'
    wait_for_reboot_log "reused sector"
    sleep 0.1
    printf 'rm /scratch/reused\r'
    wait_for_reboot_log "rm: removed /scratch/reused"
    sleep 0.1
    printf 'rm /scratch\r'
    wait_for_reboot_log "rm: removed /scratch"
    sleep 0.1
    printf 'ls /scratch\r'
    wait_for_reboot_log "ls: no such directory: /scratch"
    sleep 0.1
    printf 'exit\r'
    ) | run_qemu_to_log "$reboot_serial_log"

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

for expected in \
    "persistent mutable text" \
    "cat: cannot display binary file: /scratch/data.bin" \
    "awp hello from storage" \
    "rm: cannot remove: /scratch" \
    "write: wrote 13 bytes to /scratch/reused" \
    "rm: removed /scratch" \
    "ls: no such directory: /scratch"
do
    if ! grep -F -q "$expected" "$reboot_serial_log"; then
        echo "missing reboot mutable-filesystem output: $expected" >&2
        cat "$reboot_serial_log" >&2
        exit 1
    fi
done

reused_data=$(dd if="$test_disk" bs=512 skip=18 count=1 2>/dev/null | LC_ALL=C tr -d '\000')
if [ "$reused_data" != "reused sector" ]; then
    echo "released ARFS data sector was not reused as expected" >&2
    exit 1
fi

binary_hex=$(od -An -tx1 -N7 -j $((19 * 512)) "$test_disk" | tr -d ' \n')
if [ "$binary_hex" != "0001027f80feff" ]; then
    echo "persisted ARFS binary contents differ: $binary_hex" >&2
    exit 1
fi

echo "QEMU serial smoke test passed"
cat "$serial_log"
cat "$reboot_serial_log"
