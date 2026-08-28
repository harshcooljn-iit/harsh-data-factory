#!/usr/bin/env bash
# Run clang-format over all first-party C++ sources.
#
#   scripts/format.sh          rewrite files in place
#   scripts/format.sh --check  exit non-zero if any file would change
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

cf="${CLANG_FORMAT:-clang-format}"
if ! command -v "$cf" >/dev/null 2>&1; then
    echo "error: $cf not found (set CLANG_FORMAT)" >&2
    exit 1
fi

find_sources() {
    find src include tests examples \
        \( -name '*.cpp' -o -name '*.hpp' \) -type f | sort
}

if [[ "${1:-}" == "--check" ]]; then
    rc=0
    while IFS= read -r f; do
        if ! diff -q <("$cf" "$f") "$f" >/dev/null; then
            echo "would reformat: $f"
            rc=1
        fi
    done < <(find_sources)
    [[ $rc -eq 0 ]] && echo "all files formatted"
    exit "$rc"
fi

count=0
while IFS= read -r f; do
    "$cf" -i "$f"
    count=$((count + 1))
done < <(find_sources)
echo "formatted $count files"
