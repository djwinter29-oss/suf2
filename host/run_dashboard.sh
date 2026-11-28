#!/usr/bin/env bash
# Serve the browser dashboard locally (packing happens in the browser).
# Usage: host/run_dashboard.sh [port]     (default 8000)
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
port=${1:-8000}
echo "SUF2 dashboard: http://127.0.0.1:$port/  (Ctrl+C to stop)"
exec "${PYTHON:-python3}" -m http.server "$port" --bind 127.0.0.1 --directory "$here/dashboard"
