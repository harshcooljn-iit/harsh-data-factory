#!/usr/bin/env bash
# Run clang-tidy over the core library using a build's compile_commands.json.
#
#   scripts/tidy.sh [preset]      (default preset: debug)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

preset="${1:-debug}"
builddir="build/$preset"
ccdb="$builddir/compile_commands.json"

if [[ ! -f "$ccdb" ]]; then
    echo "error: $ccdb not found - configure first: cmake --preset $preset" >&2
    exit 1
fi

ct="${CLANG_TIDY:-clang-tidy}"
if ! command -v "$ct" >/dev/null 2>&1; then
    echo "error: $ct not found (set CLANG_TIDY)" >&2
    exit 1
fi

extra_args=()
if [[ "$(uname -s)" == "Darwin" ]] && command -v xcrun >/dev/null 2>&1; then
    # Homebrew clang-tidy does not know about the macOS SDK by default.
    extra_args+=(--extra-arg=-isysroot --extra-arg="$(xcrun --show-sdk-path)")
fi

status=0
while IFS= read -r f; do
    "$ct" -p "$builddir" "${extra_args[@]+"${extra_args[@]}"}" "$f" || status=1
done < <(find src/core -name '*.cpp' -type f | sort)
exit "$status"
