#!/usr/bin/env bash
# Run disjoint Catch2 shards concurrently, preserving every shard's output.
# Usage: bash scripts/ci-test-shards.sh TEST_BINARY SHARD_COUNT [CATCH2_ARGS...]
set -euo pipefail

test_binary=${1:?Expected a Catch2 test executable}
shards=${2:?Expected a shard count}
shift 2
if [[ ! -x "$test_binary" ]]; then
    echo "Test executable not found or not executable: $test_binary" >&2
    exit 1
fi
if [[ ! "$shards" =~ ^[1-9][0-9]*$ ]]; then
    echo "Shard count must be a positive integer: $shards" >&2
    exit 1
fi

outdir=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/magda-test-shards.XXXXXX")
echo "Running $shards Catch2 shards; logs and isolated state: $outdir"
pids=()
stop_shards() {
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait || true
}
trap 'exit 130' INT
trap 'exit 143' TERM
trap stop_shards EXIT

for ((i = 0; i < shards; i++)); do
    shard_dir="$outdir/shard-$i"
    mkdir -p "$shard_dir/tmp" "$shard_dir/config" "$shard_dir/data" "$shard_dir/cache"
    # Shared fixture names and application settings must not cross processes.
    TMPDIR="$shard_dir/tmp" \
    XDG_CONFIG_HOME="$shard_dir/config" \
    XDG_DATA_HOME="$shard_dir/data" \
    XDG_CACHE_HOME="$shard_dir/cache" \
        "$test_binary" --shard-count "$shards" --shard-index "$i" "$@" \
        > "$outdir/shard-$i.log" 2>&1 &
    pids+=("$!")
done

failed=0
for ((i = 0; i < shards; i++)); do
    rc=0
    wait "${pids[$i]}" || rc=$?
    echo "===== Catch2 shard $i/$shards (exit $rc) ====="
    cat "$outdir/shard-$i.log"
    if [[ "$rc" -ne 0 ]]; then
        failed=$((failed + 1))
    fi
done
trap - EXIT
echo "$failed of $shards Catch2 shards failed"
[[ "$failed" -eq 0 ]]
