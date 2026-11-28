#!/usr/bin/env bash
# Run the .NET SUF2 CLI, building it first if the sources changed.
# Usage: host/run_dotnet.sh pack --input app.elf --output app.suf2 --key key.pem \
#            --product-id <32 hex> --base 0x10011000
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
proj=$here/dotnet/Suf2.csproj
dll=$here/dotnet/bin/Release/net10.0/suf2.dll
command -v dotnet >/dev/null || { echo "error: .NET SDK 10 not found" >&2; exit 1; }
if [[ ! -f $dll || -n $(find "$here/dotnet" -maxdepth 1 \( -name '*.cs' -o -name '*.csproj' \) -newer "$dll") ]]; then
    dotnet build "$proj" -c Release --nologo -v quiet >&2
fi
exec dotnet "$dll" "$@"
