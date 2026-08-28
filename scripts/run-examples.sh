#!/usr/bin/env bash
# Run every example pipeline against a fresh state directory.
#
#   scripts/run-examples.sh [preset]   (default preset: debug)
set -euo pipefail

preset="${1:-debug}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/build/$preset/bin/flowforge"
examples_dir="$root/build/$preset/examples"

if [[ ! -x "$bin" ]]; then
    echo "error: $bin not found - build first: cmake --build --preset $preset" >&2
    exit 1
fi

state="$(mktemp -d)"
trap 'rm -rf "$state"' EXIT

status=0
for ex in hello python_pipeline cpp_pipeline parallel_pipeline \
          failure_pipeline retry_pipeline cache_pipeline; do
    pipeline="$examples_dir/$ex/pipeline.json"
    [[ -f "$pipeline" ]] || { echo "skip $ex (not configured)"; continue; }

    # Reset per-example scratch so runs are reproducible.
    rm -f "$examples_dir/$ex/attempts.count"
    printf 'cache example input v1\n' > "$examples_dir/cache_pipeline/data.in" 2>/dev/null || true

    echo "=============================================================="
    echo "  $ex"
    echo "=============================================================="
    extra=()
    [[ "$ex" == "parallel_pipeline" ]] && extra=(--max-concurrency 3)
    # failure_pipeline is expected to fail; don't let it abort the loop.
    if "$bin" run "$pipeline" --state-dir "$state/$ex" --plain ${extra[@]+"${extra[@]}"}; then
        :
    else
        rc=$?
        if [[ "$ex" != "failure_pipeline" ]]; then
            status=$rc
        fi
    fi
    echo
done

echo "cache_pipeline second run (expect CACHED):"
"$bin" run "$examples_dir/cache_pipeline/pipeline.json" \
    --state-dir "$state/cache_pipeline" --plain || true

exit "$status"
