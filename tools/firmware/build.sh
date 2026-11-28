#!/usr/bin/env bash
# Build the SUF2 loader and example app, and optionally run the host tests.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/firmware/build.sh [options] [rp2040|rp2350|esp32s3|all] [-- extra cmake args]

Builds the SUF2 loader and the blink example into build/<board>.
all includes RP2040, RP2350 and ESP32-S3. ESP32-S3 requires installed ESP-IDF tools.

Options:
  --key FILE      private key used to sign blink.suf2
                  (default: test-keys/local/signing-private.pem, created if missing)
  --no-sign       do not pack blink.suf2
  --test          also run Ceedling, native library and loader simulation tests
  --test-only     run the tests and build no board
  --clean         delete the build folder first
  --debug         Debug build instead of MinSizeRel
  -h, --help      show this help

Examples:
  tools/firmware/build.sh rp2040
  tools/firmware/build.sh esp32s3
  tools/firmware/build.sh all --test
  tools/firmware/build.sh rp2350 -- -DPICO_BOARD=pimoroni_pico_plus2_rp2350
EOF
}

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
board=rp2040
key=""
sign=1
test=0
build_boards=1
clean=0
build_type=""
extra=()

while [[ $# -gt 0 ]]; do
    case $1 in
        rp2040|rp2350|esp32s3|all) board=$1 ;;
        --key) key=$(realpath "$2"); shift ;;
        --no-sign) sign=0 ;;
        --test) test=1 ;;
        --test-only) test=1; build_boards=0 ;;
        --clean) clean=1 ;;
        --debug) build_type=Debug ;;
        -h|--help) usage; exit 0 ;;
        --) shift; extra=("$@"); break ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

need_submodules() {
    if [[ ! -f $repo/firmware/lib/mbedtls/CMakeLists.txt ]]; then
        echo "==> fetching Mbed TLS"
        git -C "$repo" submodule update --init firmware/lib/mbedtls
    fi
    if [[ $1 == sdk && ! -f $repo/firmware/lib/pico-sdk/lib/tinyusb/src/tusb.h ]]; then
        echo "==> fetching pico-sdk and TinyUSB"
        git -C "$repo" submodule update --init firmware/lib/pico-sdk
        git -C "$repo/firmware/lib/pico-sdk" submodule update --init lib/tinyusb
    fi
}

prepare_signing_key() {
    if [[ -z $key ]]; then
        key=$repo/test-keys/local/signing-private.pem
    fi
    if [[ $key == "$repo/test-keys/local/signing-private.pem" ]]; then
        if [[ ! -f $key ]]; then
            echo "==> generating local test keys"
            python3 "$repo/test-keys/generate-test-keys.py"
        elif [[ ! -f $repo/test-keys/local/encryption-key.jwk ]]; then
            echo "==> adding a local test AES key"
            python3 "$repo/test-keys/generate-test-keys.py" --aes-only
        fi
    fi
}

run_tests() {
    echo "==> Ceedling unit tests"
    [[ $clean == 1 ]] && "$repo/tools/firmware/run_unit_tests.sh" clobber
    "$repo/tools/firmware/run_unit_tests.sh"
    need_submodules core
    local dir
    for src in firmware/suf2 firmware/boards/rp2/suf2-boot/tests; do
        dir=$repo/build/$(basename "$(dirname "$src")")-$(basename "$src")
        [[ $clean == 1 ]] && rm -rf "$dir"
        echo "==> testing $src"
        cmake -S "$repo/$src" -B "$dir" "${generator[@]}"
        cmake --build "$dir"
        ctest --test-dir "$dir" --output-on-failure
    done
}

build_board() {
    local b=$1 dir=$repo/build/$1 args=()
    need_submodules sdk
    [[ $clean == 1 ]] && rm -rf "$dir"
    if [[ -f $dir/CMakeCache.txt ]] &&
        ! grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$repo/firmware/boards/rp2" "$dir/CMakeCache.txt"; then
        echo "error: $dir uses the old source layout; run tools/firmware/build.sh $b --clean" >&2
        exit 1
    fi
    if [[ $sign == 1 ]]; then
        prepare_signing_key
        args+=("-DSUF2_SIGNING_KEY=$key")
    else
        args+=("-USUF2_SIGNING_KEY")
    fi
    [[ -n $build_type ]] && args+=("-DCMAKE_BUILD_TYPE=$build_type")
    echo "==> building $b"
    cmake -S "$repo/firmware/boards/rp2" -B "$dir" "${generator[@]}" \
        "-DSUF2_BOARD=$b" "${args[@]}" "${extra[@]}"
    cmake --build "$dir"
    echo
    echo "$b outputs:"
    for f in suf2_loader.uf2 suf2_loader.elf blink/blink.suf2; do
        [[ -f $dir/$f ]] && echo "  ${dir#"$repo"/}/$f"
    done
}

build_esp32() {
    local dir=$repo/build/esp32s3 args=()
    if [[ ! -f $repo/firmware/lib/esp-idf/export.sh ]]; then
        git -C "$repo" submodule update --init --recursive firmware/lib/esp-idf
    fi
    if [[ ! -f $repo/firmware/lib/tinyusb/src/tusb.h ]]; then
        git -C "$repo" submodule update --init firmware/lib/tinyusb
    fi
    # shellcheck source=tools/firmware/esp_idf_env.sh
    source "$repo/tools/firmware/esp_idf_env.sh"
    suf2_esp32_environment "$repo"
    [[ $clean == 1 ]] && rm -rf "$dir"
    if [[ $sign == 1 ]]; then
        prepare_signing_key
        args+=("-DSUF2_SIGNING_KEY=$key")
    else
        args+=("-USUF2_SIGNING_KEY")
    fi
    [[ -n $build_type ]] && args+=("-DCMAKE_BUILD_TYPE=$build_type")
    local project
    for project in suf2-boot blink; do
        echo "==> building esp32s3/$project"
        idf.py -C "$repo/firmware/boards/esp32s3/$project" -B "$dir/$project" \
            "${args[@]}" "${extra[@]}" build
    done
    rm -f "$dir/blink/blink.suf2"
    if [[ $sign == 1 ]]; then
        local config=$dir/suf2-boot/generated/suf2_config.h product base
        product=$(sed -n 's/^#define SUF2_PRODUCT_ID_HEX "\(.*\)"/\1/p' "$config")
        base=$(sed -n 's/^#define SUF2_EXECUTION_BASE \(0x[[:xdigit:]]*\)u$/\1/p' "$config")
        [[ -n $product && -n $base ]] || { echo "error: ESP32-S3 generated policy is incomplete" >&2; exit 1; }
        "$repo/tools/host/run_python.sh" pack --input "$dir/blink/blink.bin" \
            --output "$dir/blink/blink.suf2" --key "$key" --product-id "$product" --base "$base"
    fi
    echo "esp32s3 outputs: build/esp32s3/suf2-boot/{bootloader/bootloader.bin,suf2_boot.bin}"
    echo "  build/esp32s3/blink/blink.bin (install only as a signed SUF2 package)"
    [[ $sign == 0 ]] || echo "  build/esp32s3/blink/blink.suf2"
}

[[ $test == 1 ]] && run_tests
if [[ $build_boards == 1 ]]; then
    if [[ $board == all ]]; then
        build_board rp2040
        build_board rp2350
        build_esp32
    elif [[ $board == esp32s3 ]]; then
        build_esp32
    else
        build_board "$board"
    fi
fi
