#!/usr/bin/env bash
# Runs the [.eval] Catch2 suite for one provider and writes its results to the job summary.
# Usage: run-evals.sh <magda_tests binary> <label>. MAGDA_EVAL_* come from the environment.
set -uo pipefail

bin="${1:?Usage: run-evals.sh <magda_tests binary> <label>}"
label="${2:?Usage: run-evals.sh <magda_tests binary> <label>}"
summary="${GITHUB_STEP_SUMMARY:-/dev/stdout}"
model="${MAGDA_EVAL_MODEL:-default}"

if [ -z "${MAGDA_EVAL_API_KEY:-}" ]; then
    printf '### %s (%s)\n\nSkipped: no API key.\n\n' "$label" "$model" >> "$summary"
    exit 0
fi

xml="eval-${label}.xml"
"$bin" "[.eval]" --reporter console --reporter "junit::out=${xml}" 2>&1 | tee "eval-${label}.log"
status=${PIPESTATUS[0]}

python3 -I - "$xml" "$label" "$model" >> "$summary" <<'EOF'
import sys
import xml.etree.ElementTree as ET

path, label, model = sys.argv[1:4]
print(f"### {label} ({model})\n")
print("| Request | Result |")
print("|---|---|")
try:
    cases = ET.parse(path).getroot().iter("testcase")
except (ET.ParseError, FileNotFoundError) as error:
    print(f"| (no report: {error}) | error |")
    cases = []
for case in cases:
    name = case.get("name", "").split("/")[-1]
    failed = case.find("failure") is not None or case.find("error") is not None
    skipped = case.find("skipped") is not None
    print(f"| {name} | {'FAIL' if failed else 'skipped' if skipped else 'pass'} |")
print()
EOF

exit "$status"
