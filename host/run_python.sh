#!/usr/bin/env bash
# Run the Python SUF2 CLI from the source tree (no install needed).
# Usage: host/run_python.sh pack --input app.elf --output app.suf2 --key key.pem \
#            --product-id <32 hex> --base 0x10011000
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python=${PYTHON:-python3}
if ! "$python" -c 'import cryptography' 2>/dev/null; then
    echo "error: the Python 'cryptography' package is missing; run: $python -m pip install cryptography" >&2
    exit 1
fi
export PYTHONPATH="$here/python/src${PYTHONPATH:+:$PYTHONPATH}"
exec "$python" -m suf2 "$@"
