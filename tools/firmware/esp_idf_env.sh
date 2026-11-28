#!/usr/bin/env bash
# Shared ESP-IDF setup for the build and flash runners.

suf2_esp32_environment() {
    local root=$1
    local idf=${IDF_PATH:-"$root/firmware/lib/esp-idf"}
    if [[ ! -f $idf/export.sh ]]; then
        echo "error: initialize firmware/lib/esp-idf and run its install.sh esp32s3 first" >&2
        return 1
    fi
    # ESP-IDF's exporter is not nounset-safe.
    set +u
    # shellcheck disable=SC1091
    if source "$idf/export.sh"; then
        set -u
    else
        set -u
        echo "error: ESP-IDF environment setup failed; run firmware/lib/esp-idf/install.sh esp32s3" >&2
        return 1
    fi
    if ! command -v xtensa-esp32s3-elf-gcc >/dev/null || ! command -v idf.py >/dev/null; then
        echo "error: ESP32-S3 tools are missing; run firmware/lib/esp-idf/install.sh esp32s3" >&2
        return 1
    fi
}
