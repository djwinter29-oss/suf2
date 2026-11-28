#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
# Turn a release tag (vMAJOR.MINOR.PATCH) into the plain version used by
# every release artifact. Anything else, including pre-release suffixes, is
# refused so a typo cannot publish a release.
#
# Usage: resolve-release-version.sh <tag>
set -eu

TAG="${1:-}"
if ! printf '%s' "$TAG" | grep -Eq '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'; then
    echo "Refusing version '$TAG' (expected a tag like v1.2.3)" >&2
    exit 1
fi
echo "${TAG#v}"
