#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
#
# Syntax-check every repository shell script with bash -n, then run
# ShellCheck when it is installed. Set SHELLCHECK to use a specific binary.

set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"

mapfile -t scripts < <(git ls-files --cached --others --exclude-standard \
    'tools/*.sh' | sort -u)

if [ "${#scripts[@]}" -eq 0 ]; then
    echo "no shell scripts found" >&2
    exit 1
fi

for script in "${scripts[@]}"; do
    bash -n "$script"
done
echo "bash -n: ${#scripts[@]} scripts OK"

shellcheck=${SHELLCHECK:-shellcheck}
if command -v "$shellcheck" >/dev/null 2>&1; then
    "$shellcheck" "${scripts[@]}"
    echo "shellcheck: OK"
else
    echo "shellcheck not installed; skipped"
fi
