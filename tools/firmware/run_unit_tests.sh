#!/usr/bin/env bash
# Run portable SUF2 Unity/CMock tests through the pinned Ceedling bundle.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
command -v ruby >/dev/null || {
    echo "error: Ruby not found; install Ruby and Bundler to run Ceedling" >&2
    exit 1
}
bundler=(ruby -e 'load Gem.bin_path("bundler", "bundle")')
cd "$repo/firmware/suf2"
if ! "${bundler[@]}" check; then
    echo "error: install Bundler and the gems in $repo/firmware/suf2/Gemfile; see firmware/README.md" >&2
    exit 1
fi
[[ $# -gt 0 ]] || set -- test:all
exec "${bundler[@]}" exec ceedling "$@"
