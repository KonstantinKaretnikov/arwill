#!/bin/sh
set -eu

usage() {
    cat <<'EOF'
usage: scripts/recreate_utm.sh [options]

Build a fresh Arwill image, replace an existing UTM VM while preserving its
configuration, and start the replacement.

Options:
  --image PATH      image to install (default: build/arwill.img)
  --no-build        use the existing image without running make build
  --attach          attach this terminal to the first UTM serial port
  --yes             skip the destructive confirmation prompt
  -h, --help        show this help

Environment:
  UTM_IMAGE         default image path
EOF
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
apple_script="$script_dir/recreate_utm.applescript"
utmctl=/Applications/UTM.app/Contents/MacOS/utmctl
vm_name='Arwill OS'
image_path=${UTM_IMAGE:-$repo_root/build/arwill.img}
build_image=true
attach_serial=false
confirmed=false

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
        --attach)
            attach_serial=true
            shift
            ;;
        --yes)
            confirmed=true
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

if [ "$confirmed" != true ]; then
    [ -t 0 ] || {
        echo "refusing destructive UTM replacement without --yes in a non-interactive shell" >&2
        exit 1
    }
    echo "This will stop and permanently delete the UTM VM named: $vm_name"
    echo "Its configuration will be cloned first and its only IDE drive replaced with:"
    echo "  $image_path"
    printf "Type the exact VM name to continue: "
    IFS= read -r confirmation
    [ "$confirmation" = "$vm_name" ] || { echo "cancelled" >&2; exit 1; }
fi

replacement_name="$vm_name replacement $(date +%s)-$$"
echo "Replacing UTM VM '$vm_name'..."
vm_id=$(osascript "$apple_script" "$vm_name" "$image_path" "$replacement_name")
echo "UTM VM '$vm_name' started (UUID $vm_id)."

if [ "$attach_serial" = true ]; then
    exec "$utmctl" attach "$vm_name"
fi

echo "Attach to its serial console with:"
echo "  $utmctl attach '$vm_name'"
