#!/usr/bin/env bash
# Run portable SUF2 Unity/CMock tests with the latest installed Ceedling gem.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
command -v ruby >/dev/null || {
    echo "error: Ruby not found; install Ruby to run Ceedling" >&2
    exit 1
}
cd "$repo/firmware/suf2"
if ! ruby -e 'exit(Gem::Specification.find_all_by_name("ceedling").empty? ? 1 : 0)'; then
    echo "error: Ceedling not found; run: gem install ceedling --no-document" >&2
    exit 1
fi
[[ $# -gt 0 ]] || set -- test:all
exec ruby -e 'load Gem.bin_path("ceedling", "ceedling")' -- "$@"
