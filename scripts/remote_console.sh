#!/bin/sh
set -eu

if [ "$#" -gt 2 ]; then
    echo "usage: remote_console.sh [host] [port]" >&2
    exit 2
fi

host=${1:-127.0.0.1}
port=${2:-23232}

if [ ! -t 0 ]; then
    exec nc "$host" "$port"
fi

terminal_state=$(stty -g)

restore_terminal() {
    stty "$terminal_state"
}

trap restore_terminal EXIT HUP INT TERM
stty raw -echo

status=0
nc "$host" "$port" || status=$?

restore_terminal
trap - EXIT HUP INT TERM
exit "$status"
