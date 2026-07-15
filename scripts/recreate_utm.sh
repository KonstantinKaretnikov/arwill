#!/bin/sh
set -eu

usage() {
    cat <<'EOF'
usage: scripts/recreate_utm.sh [options]

Build a fresh Arwill image, create or replace the fixed UTM VM, and start it
in UTM's built-in serial console window.

Options:
  --image PATH      image to install (default: build/arwill.img)
  --no-build        use the existing image without running make build
  -h, --help        show this help

Environment:
  UTM_IMAGE         default image path
EOF
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
apple_script="$script_dir/recreate_utm.applescript"
utmctl=/Applications/UTM.app/Contents/MacOS/utmctl
plist_buddy=/usr/libexec/PlistBuddy
vm_name='Arwill OS'
image_path=${UTM_IMAGE:-$repo_root/build/arwill.img}
build_image=true

while [ "$#" -gt 0 ]; do
    case "$1" in
        --image)
            [ "$#" -ge 2 ] || { echo "--image requires a value" >&2; exit 2; }
            image_path=$2
            shift 2
            ;;
        --no-build)
            build_image=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[ "$(uname -s)" = Darwin ] || { echo "UTM recreation requires macOS" >&2; exit 1; }
[ -x "$utmctl" ] || { echo "UTM CLI not found at $utmctl" >&2; exit 1; }
[ -x "$plist_buddy" ] || { echo "PlistBuddy not found at $plist_buddy" >&2; exit 1; }
[ -f "$apple_script" ] || { echo "missing helper: $apple_script" >&2; exit 1; }

if [ "$build_image" = true ]; then
    echo "Building fresh Arwill image..."
    make -C "$repo_root" build
fi

[ -f "$image_path" ] || { echo "image not found: $image_path" >&2; exit 1; }
image_dir=$(CDPATH= cd -- "$(dirname -- "$image_path")" && pwd)
image_path="$image_dir/$(basename -- "$image_path")"
case "$image_path" in
    *.utm/*)
        echo "refusing an image stored inside a UTM bundle that will be deleted: $image_path" >&2
        exit 1
        ;;
esac

replacement_name="$vm_name replacement $(date +%s)-$$"
echo "Replacing UTM VM '$vm_name'..."
vm_id=$(osascript "$apple_script" "$vm_name" "$image_path" "$replacement_name")

osascript -e 'tell application "UTM" to quit'
attempt=0
while [ "$(osascript -e 'application "UTM" is running')" = true ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 80 ] || { echo "UTM did not quit in time" >&2; exit 1; }
    sleep 0.25
done

vm_config="$HOME/Library/Containers/com.utmapp.UTM/Data/Documents/$vm_name.utm/config.plist"
[ -f "$vm_config" ] || { echo "UTM VM configuration not found: $vm_config" >&2; exit 1; }

"$plist_buddy" -c 'Set :QEMU:UEFIBoot false' "$vm_config"
"$plist_buddy" -c 'Set :Serial:0:Mode Terminal' "$vm_config"
"$plist_buddy" -c 'Delete :Serial:0:Terminal' "$vm_config" >/dev/null 2>&1 || true
"$plist_buddy" -c 'Add :Serial:0:Terminal dict' "$vm_config"
"$plist_buddy" -c 'Add :Serial:0:Terminal:ForegroundColor string #ffffff' "$vm_config"
"$plist_buddy" -c 'Add :Serial:0:Terminal:BackgroundColor string #000000' "$vm_config"
"$plist_buddy" -c 'Add :Serial:0:Terminal:Font string Menlo' "$vm_config"
"$plist_buddy" -c 'Add :Serial:0:Terminal:FontSize integer 12' "$vm_config"
"$plist_buddy" -c 'Add :Serial:0:Terminal:CursorBlink bool true' "$vm_config"
"$plist_buddy" -c 'Set :Network:0:Mode Shared' "$vm_config"
"$plist_buddy" -c 'Set :Network:0:IsolateFromHost false' "$vm_config"
"$plist_buddy" -c 'Delete :Network:0:VlanGuestAddress' "$vm_config" >/dev/null 2>&1 || true
"$plist_buddy" -c 'Delete :Network:0:VlanDhcpStartAddress' "$vm_config" >/dev/null 2>&1 || true
"$plist_buddy" -c 'Delete :Network:0:VlanDhcpEndAddress' "$vm_config" >/dev/null 2>&1 || true
"$plist_buddy" -c 'Add :Network:0:VlanGuestAddress string 10.0.2.0/24' "$vm_config"
"$plist_buddy" -c 'Add :Network:0:VlanDhcpStartAddress string 10.0.2.2' "$vm_config"
"$plist_buddy" -c 'Add :Network:0:VlanDhcpEndAddress string 10.0.2.14' "$vm_config"

open -a /Applications/UTM.app
attempt=0
until "$utmctl" list 2>/dev/null | grep -Fq "$vm_id"; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 80 ] || { echo "UTM did not reload VM '$vm_name' in time" >&2; exit 1; }
    sleep 0.25
done

"$utmctl" start "$vm_name"
osascript -e 'tell application "UTM" to activate'
attempt=0
until [ "$("$utmctl" status "$vm_name" 2>/dev/null)" = started ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 80 ] || { echo "UTM VM '$vm_name' did not start in time" >&2; exit 1; }
    sleep 0.25
done

echo "UTM VM '$vm_name' started in its built-in console (UUID $vm_id)."
