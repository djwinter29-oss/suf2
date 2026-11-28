#!/usr/bin/env bash
# Flash the SUF2 loader, or copy a signed app to the loader's drive.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/firmware/flash.sh [options] [rp2040|rp2350|esp32s3]

Writes build/<board>/suf2_loader to the board, then optionally installs an app.
Build first with tools/firmware/build.sh.

Loader methods (--method):
  uf2       copy suf2_loader.uf2 to the BOOTSEL drive (default).
            Hold BOOTSEL while plugging the board in first.
  picotool  load suf2_loader.uf2 with picotool over USB (board in BOOTSEL)
  swd       program suf2_loader.elf with OpenOCD and a CMSIS-DAP probe
            (for example the Raspberry Pi Debug Probe)
  serial    ESP32-S3 only: flash the custom bootloader, partition table and
            recovery app using ESP-IDF; requires --port (default for esp32s3)

Options:
  --method M      uf2, picotool, swd or serial
  --port PORT     explicit ESP32-S3 serial device (for example /dev/ttyUSB0)
  --app FILE      after the loader, copy FILE (.suf2) to the SUF2BOOT drive
  --app-only FILE only copy FILE to SUF2BOOT; do not touch the loader
  --blink         same as --app build/<board>/blink/blink.suf2
  --timeout SEC   how long to wait for a drive (default 30)
  --dry-run       print what would be done without writing anything
  -h, --help      show this help

Examples:
  tools/firmware/flash.sh rp2040
  tools/firmware/flash.sh rp2350 --method swd --blink
  tools/firmware/flash.sh --app-only my_app.suf2
  tools/firmware/flash.sh esp32s3 --port /dev/ttyUSB0 --blink --dry-run
EOF
}

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
board=rp2040
method=""
port=""
app=""
app_only=0
blink=0
timeout=30
dry=0

while [[ $# -gt 0 ]]; do
    case $1 in
        rp2040|rp2350|esp32s3) board=$1 ;;
        --method) method=$2; shift ;;
        --port) port=$2; shift ;;
        --app) app=$(realpath "$2"); shift ;;
        --app-only) app=$(realpath "$2"); app_only=1; shift ;;
        --blink) blink=1 ;;
        --timeout) timeout=$2; shift ;;
        --dry-run) dry=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

dir=$repo/build/$board
[[ -n $method ]] || { if [[ $board == esp32s3 ]]; then method=serial; else method=uf2; fi; }
[[ $blink == 1 ]] && app=$dir/blink/blink.suf2

die() { echo "error: $*" >&2; exit 1; }

run() {
    printf '+'; printf ' %q' "$@"; echo
    [[ $dry == 1 ]] || "$@"
}

# Print the mount point of the first mounted drive whose label is in $@.
find_drive() {
    local label mnt
    for label in "$@"; do
        mnt=$(lsblk -rno LABEL,MOUNTPOINT 2>/dev/null | awk -v l="$label" '$1 == l && $2 != "" { print $2; exit }')
        if [[ -n $mnt ]]; then
            printf '%b\n' "$mnt"   # lsblk escapes spaces as \x20
            return 0
        fi
    done
    return 1
}

wait_drive() {
    local what=$1 mnt; shift
    if [[ $dry == 1 ]]; then
        echo "/media/$USER/$1"
        return 0
    fi
    echo "waiting for the $what drive ($*)..." >&2
    for ((i = 0; i < timeout * 2; i++)); do
        if mnt=$(find_drive "$@"); then
            echo "$mnt"
            return 0
        fi
        sleep 0.5
    done
    die "no $what drive found after ${timeout}s; is it plugged in and mounted?"
}

copy_to() {
    local file=$1 mnt=$2
    run cp "$file" "$mnt/"
    run sync
}

flash_loader() {
    local uf2=$dir/suf2_loader.uf2 elf=$dir/suf2_loader.elf
    if [[ $board == esp32s3 && $method != serial ]]; then
        die "ESP32-S3 requires --method serial"
    fi
    case $method in
        serial)
            [[ $board == esp32s3 ]] || die "serial is supported for ESP32-S3 only"
            [[ -n $port ]] || die "ESP32-S3 flashing requires an explicit --port"
            [[ -f $dir/suf2-boot/flasher_args.json ]] || die "build ESP32-S3 first"
            # shellcheck source=tools/firmware/esp_idf_env.sh
            source "$repo/tools/firmware/esp_idf_env.sh"
            suf2_esp32_environment "$repo"
            run idf.py -C "$repo/firmware/boards/esp32s3/suf2-boot" \
                -B "$dir/suf2-boot" -p "$port" flash
            ;;
        uf2)
            [[ -f $uf2 ]] || die "$uf2 not found; run tools/firmware/build.sh $board"
            local labels=(RPI-RP2)
            [[ $board == rp2350 ]] && labels=(RP2350)
            local mnt
            mnt=$(wait_drive BOOTSEL "${labels[@]}")
            echo "copying loader to $mnt"
            copy_to "$uf2" "$mnt"
            ;;
        picotool)
            [[ -f $uf2 ]] || die "$uf2 not found; run tools/firmware/build.sh $board"
            local pt
            pt=$(command -v picotool || true)
            [[ -z $pt && -x $dir/_deps/picotool/picotool ]] && pt=$dir/_deps/picotool/picotool
            [[ -n $pt ]] || die "picotool not found; install it or build with SUF2_LOADER_UF2=ON"
            run "$pt" load -v -x "$uf2"
            ;;
        swd)
            [[ -f $elf ]] || die "$elf not found; run tools/firmware/build.sh $board"
            command -v openocd >/dev/null || die "openocd not found"
            local target=rp2040.cfg
            [[ $board == rp2350 ]] && target=rp2350.cfg
            run openocd -f interface/cmsis-dap.cfg -f "target/$target" \
                -c "adapter speed 5000" -c "program $elf verify reset exit"
            ;;
        *) die "unknown method: $method" ;;
    esac
}

install_app() {
    [[ -f $app ]] || die "$app not found"
    local mnt
    mnt=$(wait_drive SUF2BOOT SUF2BOOT)
    echo "copying $(basename "$app") to $mnt"
    copy_to "$app" "$mnt"
    echo "the loader verifies and installs it, then reboots; see STATUS.TXT if the drive comes back"
}

[[ $app_only == 1 ]] || flash_loader
[[ -n $app ]] && install_app
echo "done"
