#!/usr/bin/env bash
# parse-crash.sh — Extract crashed thread info from macOS crash logs
# Supports both .ips (JSON) and .crash (text) formats
#
# Usage: parse-crash.sh <crash-file> [--all-threads]

set -euo pipefail

FILE="${1:?Usage: parse-crash.sh <crash-file> [--all-threads]}"
ALL_THREADS="${2:-}"

if [[ ! -f "$FILE" ]]; then
    echo "Error: File not found: $FILE" >&2
    exit 1
fi

# Detect format by checking first character
FIRST_CHAR=$(head -c1 "$FILE")

if [[ "$FIRST_CHAR" == "{" ]]; then
    # === .ips JSON format ===
    # The file has a JSON header line followed by the main JSON body
    # Use python for reliable JSON parsing
    python3 -c "
import json, sys

# .ips files have a header JSON line then the main body
with open('$FILE') as f:
    lines = f.read()

# Find the start of the second JSON object
first_newline = lines.index('\n')
body = json.loads(lines[first_newline + 1:])

# Header info
exc = body.get('exception', {})
term = body.get('termination', {})
faulting = body.get('faultingThread', -1)

print('=== CRASH SUMMARY ===')
print(f'Exception: {exc.get(\"type\", \"?\")} ({exc.get(\"signal\", \"?\")}) — {exc.get(\"subtype\", \"\")}')
print(f'Termination: {term.get(\"indicator\", \"?\")}')
print(f'Crashed Thread: {faulting}')
print()

threads = body.get('threads', [])
images = body.get('usedImages', [])

# Build image lookup: index -> {name, base}
image_map = {}
for i, img in enumerate(images):
    name = img.get('name', img.get('path', '').split('/')[-1])
    base = img.get('base', 0)
    image_map[i] = {'name': name, 'base': base}

def format_thread(t, idx):
    name = t.get('name', '')
    queue = t.get('queue', '')
    header = f'Thread {idx}'
    if name: header += f': {name}'
    if queue: header += f'  (queue: {queue})'
    if idx == faulting: header += '  << CRASHED >>'
    print(header)

    for i, frame in enumerate(t.get('frames', [])):
        img_idx = frame.get('imageIndex', -1)
        img_name = image_map.get(img_idx, {}).get('name', '???')
        img_base = image_map.get(img_idx, {}).get('base', 0)
        addr = frame.get('imageOffset', 0) + img_base
        symbol = frame.get('symbol', '')
        src_line = frame.get('sourceLine', '')
        src_file = frame.get('sourceFile', '')

        line = f'  {i:<3} {img_name:<40} 0x{addr:016x}'
        if symbol:
            line += f'  {symbol}'
            offset = frame.get('symbolLocation', 0)
            if offset: line += f' + {offset}'
        if src_file:
            line += f'  [{src_file}'
            if src_line: line += f':{src_line}'
            line += ']'
        print(line)
    print()

if '$ALL_THREADS' == '--all-threads':
    for i, t in enumerate(threads):
        format_thread(t, i)
else:
    # Just the crashed thread
    if 0 <= faulting < len(threads):
        format_thread(threads[faulting], faulting)
    else:
        print('Could not find crashed thread in log')
"
else
    # === .crash text format ===
    # Extract header (up to first blank line after crash info)
    echo "=== CRASH SUMMARY ==="
    grep -E "^(Exception Type|Exception Codes|Termination Reason|Crashed Thread|Process|Date/Time):" "$FILE" 2>/dev/null || true
    echo

    # Find which thread crashed
    CRASHED_THREAD=$(grep "^Crashed Thread:" "$FILE" | grep -oE '[0-9]+' | head -1)
    if [[ -z "$CRASHED_THREAD" ]]; then
        echo "Could not determine crashed thread" >&2
        exit 1
    fi

    if [[ "$ALL_THREADS" == "--all-threads" ]]; then
        # Print all threads
        sed -n '/^Thread [0-9]/,/^$/p' "$FILE"
    else
        # Extract just the crashed thread's stack
        sed -n "/^Thread ${CRASHED_THREAD} Crashed/,/^$/p" "$FILE"
        # Also try "Thread N::" format if above didn't match
        if ! grep -q "^Thread ${CRASHED_THREAD} Crashed" "$FILE" 2>/dev/null; then
            sed -n "/^Thread ${CRASHED_THREAD}::/,/^$/p" "$FILE"
        fi
    fi
fi
