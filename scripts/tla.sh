#!/usr/bin/env bash
# Model-check every spec under specs/tla with TLC (#2863).
# Usage: scripts/tla.sh [spec-dir-name] [cfg-name]   e.g. scripts/tla.sh plan_swap Safety
set -euo pipefail

VERSION="v1.7.4"
SHA256="936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/magda"
JAR="$CACHE/tla2tools-$VERSION.jar"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ONLY_SPEC="${1:-}"
ONLY_CFG="${2:-}"

if [ ! -f "$JAR" ]; then
    mkdir -p "$CACHE"
    curl -sSfL -o "$JAR.part" \
        "https://github.com/tlaplus/tlaplus/releases/download/$VERSION/tla2tools.jar"
    echo "$SHA256  $JAR.part" | shasum -a 256 -c - >/dev/null
    mv "$JAR.part" "$JAR"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

status=0
for dir in "$ROOT"/specs/tla/*/; do
    name="$(basename "$dir")"
    if [ -n "$ONLY_SPEC" ] && [ "$name" != "$ONLY_SPEC" ]; then continue; fi
    for cfg in "$dir"*.cfg; do
        cfgName="$(basename "$cfg" .cfg)"
        if [ -n "$ONLY_CFG" ] && [ "$cfgName" != "$ONLY_CFG" ]; then continue; fi
        module="$(ls "$dir"*.tla | head -1)"
        echo "== $name / $cfgName"
        # TLC writes states and traces next to the spec; run a copy so the tree stays clean.
        rm -rf "$WORK/run" && mkdir -p "$WORK/run"
        cp "$dir"*.tla "$dir"*.cfg "$WORK/run/"
        if ! (cd "$WORK/run" && java -XX:+UseParallelGC -cp "$JAR" tlc2.TLC -deadlock \
                -workers auto -config "$(basename "$cfg")" "$(basename "$module" .tla)" \
                | grep -v "^Parsing\|^Semantic\|^Progress"); then
            status=1
        fi
    done
done
exit $status
