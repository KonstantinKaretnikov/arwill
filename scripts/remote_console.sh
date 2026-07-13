#!/bin/sh
set -eu

if [ "$#" -gt 2 ]; then
    echo "usage: remote_console.sh [host] [port]" >&2
    exit 2
fi

host=${1:-127.0.0.1}
port=${2:-23232}

terminal_device=/dev/tty
if ! terminal_state=$(stty -g 2>/dev/null < "$terminal_device"); then
    exec nc "$host" "$port"
fi

restore_terminal() {
    stty "$terminal_state" < "$terminal_device"
}

trap restore_terminal EXIT HUP INT TERM
stty raw -echo < "$terminal_device"

status=0
nc "$host" "$port" < "$terminal_device" || status=$?

restore_terminal
trap - EXIT HUP INT TERM
exit "$status"
