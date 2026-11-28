#!/usr/bin/env bash
# Run all host tool tests: Python, .NET, dashboard and cross-producer checks.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python=${PYTHON:-python3}
export PYTHONPATH="$here/python/src${PYTHONPATH:+:$PYTHONPATH}"
echo "==> Python"
"$python" -m unittest discover -s "$here/python/tests"
if command -v dotnet >/dev/null; then
    echo "==> .NET"
    dotnet build "$here/dotnet/Suf2.csproj" -c Release --nologo -v quiet
    "$python" "$here/dotnet/selfcheck.py"
else
    echo "==> .NET skipped (dotnet not found)"
fi
if command -v npm >/dev/null; then
    echo "==> dashboard"
    npm --prefix "$here/dashboard" test --silent
else
    echo "==> dashboard skipped (npm not found)"
fi
echo "==> cross-producer"
"$python" -m unittest discover -s "$here/shared/tests"
