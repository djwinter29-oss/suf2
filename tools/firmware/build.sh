#!/usr/bin/env bash
# Build the SUF2 loader and example app, and optionally run the host tests.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/firmware/build.sh [options] [rp2040|rp2350|all] [-- extra cmake args]

Builds the SUF2 loader and the blink example into build/<board>.

Options:
  --key FILE      private key used to sign blink.suf2
                  (default: host/shared/local/signing-private.pem, created if missing)
  --no-sign       do not pack blink.suf2
  --test          also run the library and loader simulation tests
  --test-only     run the tests and build no board
  --clean         delete the build folder first
  --debug         Debug build instead of MinSizeRel
  -h, --help      show this help

Examples:
  tools/firmware/build.sh rp2040
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
        rp2040|rp2350|all) board=$1 ;;
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

run_tests() {
    need_submodules core
    local dir
    for src in firmware/suf2 firmware/boards/rp2-common/tests; do
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
    if [[ $sign == 1 ]]; then
        if [[ -z $key ]]; then
            key=$repo/host/shared/local/signing-private.pem
            if [[ ! -f $key ]]; then
                echo "==> generating local test keys"
                python3 "$repo/host/shared/generate-test-keys.py"
            fi
        fi
        args+=("-DSUF2_SIGNING_KEY=$key")
    else
        args+=("-USUF2_SIGNING_KEY")
    fi
    [[ -n $build_type ]] && args+=("-DCMAKE_BUILD_TYPE=$build_type")
    echo "==> building $b"
    cmake -S "$repo/firmware/boards/$b" -B "$dir" "${generator[@]}" "${args[@]}" "${extra[@]}"
    cmake --build "$dir"
    echo
    echo "$b outputs:"
    for f in suf2_loader.uf2 suf2_loader.elf blink/blink.suf2; do
        [[ -f $dir/$f ]] && echo "  ${dir#"$repo"/}/$f"
    done
}

[[ $test == 1 ]] && run_tests
if [[ $build_boards == 1 ]]; then
    if [[ $board == all ]]; then
        build_board rp2040
        build_board rp2350
    else
        build_board "$board"
    fi
fi
