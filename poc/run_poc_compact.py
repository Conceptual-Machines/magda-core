#!/usr/bin/env python3
"""POC: Compact assembler-like syntax — model outputs minimal tokens,
we reconstruct full MAGDA DSL at runtime.

Compact format:
  TRACK <name>                          → track(name="<name>", new=true)
  DEL <id>                              → track(id=<id>).delete()
  MUTE <name>                           → filter(tracks, track.name == "<name>").track.set(mute=true)
  SOLO <name>                           → filter(tracks, track.name == "<name>").track.set(solo=true)
  SET <id|name> vol=<dB> pan=<v> ...    → track(...).track.set(...)
  CLIP <id|name> <bar> <length_bars>    → track(...).clip.new(bar=<bar>, length_bars=<len>)
  FX <id|name> <fx_name>               → track(...).fx.add(name="<fx>")
  ARP <root> <quality> <beat> <step> [beats]  → .notes.add_arpeggio(...)
  CHORD <root> <quality> <beat> <length> [vel] → .notes.add_chord(...)
  NOTE <pitch> <beat> <length> [vel]    → .notes.add(...)
"""

import argparse
import json
import re
import time
from pathlib import Path

import requests

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
LLAMA_SERVER_URL = "http://127.0.0.1:8080"
MODELS = {
    "32b":  "/Volumes/External SSD/models/qwen2.5-coder-32b/Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf",
    "7b":   "/Volumes/External SSD/models/qwen2.5-coder-7b/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
    "3b":   "/Volumes/External SSD/models/qwen2.5-coder-3b/Qwen2.5-Coder-3B-Instruct-Q4_K_M.gguf",
    "1.5b": "/Volumes/External SSD/models/qwen2.5-coder-1.5b/Qwen2.5-Coder-1.5B-Instruct-Q4_K_M.gguf",
}
RESULTS_FILE = Path(__file__).parent / "results" / "results_compact.jsonl"

# ---------------------------------------------------------------------------
# GBNF grammar for the compact syntax
# ---------------------------------------------------------------------------
COMPACT_GBNF = r"""
root ::= ws? instruction (nl instruction)* ws?
instruction ::= track-inst | del-inst | mute-inst | solo-inst | set-inst | clip-inst | fx-inst | arp-inst | chord-inst | note-inst

track-inst ::= "TRACK" sp name
del-inst ::= "DEL" sp ref
mute-inst ::= "MUTE" sp name
solo-inst ::= "SOLO" sp name
set-inst ::= "SET" sp ref (sp kv)+
clip-inst ::= "CLIP" sp ref sp number sp number
fx-inst ::= "FX" sp ref sp name
arp-inst ::= "ARP" sp pitch sp quality sp number sp number (sp number)?
chord-inst ::= "CHORD" sp pitch sp quality sp number sp number (sp number)?
note-inst ::= "NOTE" sp pitch sp number sp number (sp number)?

ref ::= number | name
kv ::= identifier "=" value
value ::= number | "true" | "false"
quality ::= "major" | "minor" | "min" | "dim" | "aug" | "sus2" | "sus4"
pitch ::= [A-G] [#b]? [0-9]
name ::= [A-Za-z] [A-Za-z0-9_-]*
identifier ::= [a-z_] [a-z0-9_]*
number ::= "-"? [0-9]+ ("." [0-9]+)?
sp ::= " "+
nl ::= "\n"
ws ::= [ \t\n\r]+
"""

# ---------------------------------------------------------------------------
# Compact → DSL reconstruction
# ---------------------------------------------------------------------------
def _ref_to_target(ref: str) -> str:
    """Convert a ref (number or name) to track(...) DSL."""
    try:
        n = int(ref)
        return f"track(id={n})"
    except ValueError:
        return f'track(name="{ref}")'


def compact_to_dsl(compact: str) -> str:
    """Convert compact assembler output to full MAGDA DSL."""
    lines = compact.strip().splitlines()
    dsl_lines = []
    # Track context for note/arp/chord that follow a CLIP
    last_clip_target = None

    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        parts = line.split()
        op = parts[0].upper()

        if op == "TRACK":
            name = " ".join(parts[1:])
            dsl_lines.append(f'track(name="{name}", new=true)')
            last_clip_target = f'track(name="{name}")'

        elif op == "DEL":
            dsl_lines.append(f"{_ref_to_target(parts[1])}.delete()")

        elif op == "MUTE":
            name = " ".join(parts[1:])
            dsl_lines.append(f'filter(tracks, track.name == "{name}").track.set(mute=true)')

        elif op == "SOLO":
            name = " ".join(parts[1:])
            dsl_lines.append(f'filter(tracks, track.name == "{name}").track.set(solo=true)')

        elif op == "SET":
            ref = parts[1]
            target = _ref_to_target(ref)
            kvs = parts[2:]
            prop_str = ", ".join(kvs)
            dsl_lines.append(f"{target}.track.set({prop_str})")

        elif op == "CLIP":
            ref = parts[1]
            bar = parts[2]
            length = parts[3]
            target = _ref_to_target(ref)
            last_clip_target = target
            dsl_lines.append(f"{target}.clip.new(bar={bar}, length_bars={length})")

        elif op == "FX":
            ref = parts[1]
            fx = parts[2]
            target = _ref_to_target(ref)
            dsl_lines.append(f'{target}.fx.add(name="{fx}")')

        elif op == "ARP":
            root = parts[1]
            quality = parts[2]
            beat = parts[3]
            step = parts[4]
            beats = parts[5] if len(parts) > 5 else None
            arp_args = f"root={root}, quality={quality}, beat={beat}, step={step}"
            if beats:
                arp_args += f", beats={beats}"
            target = last_clip_target or "track(id=1)"
            dsl_lines.append(f"{target}.notes.add_arpeggio({arp_args})")

        elif op == "CHORD":
            root = parts[1]
            quality = parts[2]
            beat = parts[3]
            length = parts[4]
            vel = parts[5] if len(parts) > 5 else None
            chord_args = f"root={root}, quality={quality}, beat={beat}, length={length}"
            if vel:
                chord_args += f", velocity={vel}"
            target = last_clip_target or "track(id=1)"
            dsl_lines.append(f"{target}.notes.add_chord({chord_args})")

        elif op == "NOTE":
            pitch = parts[1]
            beat = parts[2]
            length = parts[3]
            vel = parts[4] if len(parts) > 4 else None
            note_args = f"pitch={pitch}, beat={beat}, length={length}"
            if vel:
                note_args += f", velocity={vel}"
            target = last_clip_target or "track(id=1)"
            dsl_lines.append(f"{target}.notes.add({note_args})")

        else:
            dsl_lines.append(f"# UNKNOWN: {line}")

    return "\n".join(dsl_lines)


# ---------------------------------------------------------------------------
# System prompt
# ---------------------------------------------------------------------------
SYSTEM_PROMPT = """You are MAGDA, an AI assistant for a DAW.
Respond ONLY with compact instructions. No prose. No markdown. One instruction per line.

INSTRUCTIONS:
  TRACK <name>                        - Create new track
  DEL <id>                            - Delete track by index
  MUTE <name>                         - Mute tracks by name
  SOLO <name>                         - Solo tracks by name
  SET <id|name> key=val key=val ...   - Set track props (vol, pan, mute, solo)
  CLIP <id|name> <bar> <length_bars>  - Add clip at bar
  FX <id|name> <fx_name>             - Add effect
  ARP <root> <quality> <beat> <step> [beats] - Add arpeggio
  CHORD <root> <quality> <beat> <len> [vel]  - Add chord
  NOTE <pitch> <beat> <length> [vel]  - Add note

EXAMPLES:
"create a bass track" ->
TRACK Bass

"delete track 1" ->
DEL 1

"mute all tracks named Drums" ->
MUTE Drums

"add a 4 bar clip at bar 1 on track 2" ->
CLIP 2 1 4

"add reverb and EQ to Vocals" ->
FX Vocals reverb
FX Vocals eq

"create synth track, add 2 bar clip with C major arpeggio" ->
TRACK Synth
CLIP Synth 1 2
ARP C4 major 0 0.5

"set volume of track 3 to -6 dB and pan left" ->
SET 3 vol=-6 pan=-1

"E minor to C major progression over 4 bars on track 1" ->
CLIP 1 1 4
ARP E4 min 0 0.5 8
ARP C4 major 8 0.5 8"""

# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------
TEST_CASES = [
    {
        "prompt": "create a bass track",
        "expected_contains": ["track(", "Bass"],
    },
    {
        "prompt": "delete track 2",
        "expected_contains": ["track(id=2)", ".delete()"],
    },
    {
        "prompt": "mute all tracks named Drums",
        "expected_contains": ["filter(tracks", "Drums", "mute=true"],
    },
    {
        "prompt": "add a 4 bar clip at bar 1 on track 1",
        "expected_contains": ["track(id=1)", "clip.new(", "length_bars=4"],
    },
    {
        "prompt": "add reverb and EQ to the Vocals track",
        "expected_contains": ["Vocals", "fx.add(", "reverb", "eq"],
    },
    {
        "prompt": "create a synth track and add a 2 bar clip with a C major arpeggio",
        "expected_contains": ["clip.new(", "add_arpeggio(", "C4", "major"],
    },
    {
        "prompt": "set volume of track 3 to -6 dB and pan it left",
        "expected_contains": ["track(id=3)", "set(", "vol=-6"],
    },
    {
        "prompt": "add an E minor to C major chord progression over 4 bars on track 1",
        "expected_contains": ["E4", "min", "C4", "major"],
    },
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def wait_for_server(timeout=30):
    print(f"Waiting for llama-server at {LLAMA_SERVER_URL}...", end="", flush=True)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            r = requests.get(f"{LLAMA_SERVER_URL}/health", timeout=2)
            if r.status_code == 200:
                print(" ready.")
                return True
        except requests.exceptions.ConnectionError:
            pass
        time.sleep(0.5)
    print(" TIMEOUT")
    return False


def generate(prompt: str, use_grammar: bool = False) -> tuple[str, str, dict]:
    """Return (compact_output, reconstructed_dsl, timing)."""
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.1,
        "max_tokens": 256,
    }
    if use_grammar:
        payload["grammar"] = COMPACT_GBNF.strip()

    t0 = time.perf_counter()
    r = requests.post(f"{LLAMA_SERVER_URL}/v1/chat/completions", json=payload, timeout=60)
    wall_s = round(time.perf_counter() - t0, 3)

    r.raise_for_status()
    body = r.json()

    compact = body["choices"][0]["message"]["content"].strip()
    dsl = compact_to_dsl(compact)

    t = body.get("timings", {})
    usage = body.get("usage", {})
    timing = {
        "wall_s": wall_s,
        "prompt_tokens": usage.get("prompt_tokens"),
        "completion_tokens": usage.get("completion_tokens"),
        "prompt_ms": t.get("prompt_ms"),
        "predicted_ms": t.get("predicted_ms"),
        "tokens_per_sec": round(t["predicted_per_second"], 1) if t.get("predicted_per_second") else None,
    }
    return compact, dsl, timing


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--32b", dest="model", action="store_const", const="32b")
    parser.add_argument("--7b",  dest="model", action="store_const", const="7b")
    parser.add_argument("--3b",  dest="model", action="store_const", const="3b")
    parser.add_argument("--1.5b", dest="model", action="store_const", const="1.5b")
    parser.add_argument("--grammar", action="store_true", help="Use GBNF grammar constraint")
    args = parser.parse_args()
    model_key = args.model or "7b"
    model_path = MODELS[model_key]

    if not wait_for_server():
        return

    model_label = Path(model_path).stem
    mode = "GBNF-constrained" if args.grammar else "unconstrained"
    print()
    print("=" * 70)
    print(f"MAGDA DSL POC (COMPACT) — {model_label} [{mode}]")
    print("=" * 70)

    total_score = 0
    total_possible = 0
    total_elapsed = 0.0
    case_results = []

    for i, tc in enumerate(TEST_CASES, 1):
        prompt = tc["prompt"]
        expected = tc["expected_contains"]
        print(f"\n[{i}/{len(TEST_CASES)}] {prompt}")
        print("-" * 60)

        try:
            compact, dsl, timing = generate(prompt, use_grammar=args.grammar)

            hits = sum(1 for e in expected if e.lower() in dsl.lower())
            score = hits
            possible = len(expected)
            passed = hits == possible
            total_score += score
            total_possible += possible
            total_elapsed += timing["wall_s"]

            tok_s = timing.get("tokens_per_sec", "?")
            p_ms = timing.get("prompt_ms", "?")
            g_ms = timing.get("predicted_ms", "?")
            print(f"  Compact: {compact}")
            print(f"  DSL:     {dsl}")
            print(f"  Wall:    {timing['wall_s']:.2f}s  (prompt {p_ms}ms  gen {g_ms}ms  {tok_s} tok/s)")
            print(f"  Tokens:  prompt={timing['prompt_tokens']}  completion={timing['completion_tokens']}")
            print(f"  Score:   {score}/{possible} — {'PASS' if passed else 'FAIL'}")

            case_results.append({
                "prompt": prompt,
                "compact": compact,
                "dsl": dsl,
                "timing": timing,
                "score": score,
                "possible": possible,
                "passed": passed,
            })

        except Exception as e:
            print(f"  ERROR: {e}")
            case_results.append({
                "prompt": prompt,
                "error": str(e),
                "score": 0,
                "possible": len(expected),
                "passed": False,
            })

    avg = total_elapsed / len(TEST_CASES) if TEST_CASES else 0
    pct = round(total_score / total_possible * 100) if total_possible else 0

    print()
    print("=" * 70)
    print(f"TOTAL: {total_score}/{total_possible} ({pct}%)  |  avg latency: {avg:.2f}s")
    print("=" * 70)

    RESULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    run = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "model": model_label,
        "mode": f"compact_{mode}",
        "score_pct": pct,
        "avg_elapsed_s": round(avg, 3),
        "cases": case_results,
    }
    with open(RESULTS_FILE, "a") as f:
        f.write(json.dumps(run) + "\n")
    print(f"Results saved to {RESULTS_FILE}")


if __name__ == "__main__":
    main()
