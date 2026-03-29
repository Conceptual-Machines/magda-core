#!/usr/bin/env python3
"""POC: Qwen2.5-Coder-7B for chord progression generation via compact syntax."""

import json
import time
from pathlib import Path

import requests

LLAMA_SERVER_URL = "http://127.0.0.1:8080"
RESULTS_FILE = Path(__file__).parent / "results" / "results_qwen_chords.jsonl"

SYSTEM_PROMPT = """You are MAGDA, an AI assistant for a DAW.
Respond ONLY with compact instructions. No prose. No markdown. One instruction per line.

CHORD <root> <quality> <beat> <length>
  root: note + octave (C4, D3, Bb4, F#3)
  quality: major, minor, min, dim, aug, dom7, maj7, min7, dim7, sus2, sus4, min9, maj9, dom9
  beat: start position in beats (0-based)
  length: duration in beats

EXAMPLES:
"ii-V-I in C major" ->
CHORD D3 min7 0 4
CHORD G3 dom7 4 4
CHORD C3 maj7 8 4

"I-V-vi-IV in G major, 4 bars" ->
CHORD G3 major 0 4
CHORD D3 major 4 4
CHORD E3 minor 8 4
CHORD C3 major 12 4

"12 bar blues in G" ->
CHORD G3 dom7 0 4
CHORD G3 dom7 4 4
CHORD G3 dom7 8 4
CHORD G3 dom7 12 4
CHORD C3 dom7 16 4
CHORD C3 dom7 20 4
CHORD G3 dom7 24 4
CHORD G3 dom7 28 4
CHORD D3 dom7 32 4
CHORD C3 dom7 36 4
CHORD G3 dom7 40 4
CHORD D3 dom7 44 4

"sad progression in A minor" ->
CHORD A3 minor 0 4
CHORD F3 major 4 4
CHORD C3 major 8 4
CHORD G3 major 12 4"""

GBNF = r"""
root ::= ws? line (nl line)* ws?
line ::= "CHORD" sp note sp quality sp number sp number
note ::= [A-G] [#b]? [0-9]
quality ::= "major" | "minor" | "min" | "dim" | "aug" | "dom7" | "maj7" | "min7" | "dim7" | "sus2" | "sus4" | "min9" | "maj9" | "dom9" | "min11" | "maj11" | "dom11" | "min13" | "maj13" | "dom13"
number ::= [0-9]+ ("." [0-9]+)?
sp ::= " "+
nl ::= "\n"
ws ::= [ \t\n\r]+
"""

TEST_CASES = [
    {
        "prompt": "simple chord progression in C major, 4 bars",
        "expected_contains": ["C", "major"],
        "expected_any": ["F", "G", "Am", "minor"],
    },
    {
        "prompt": "ii-V-I progression in C major",
        "expected_contains": ["D", "min", "G", "dom7", "C", "maj"],
        "expected_any": [],
    },
    {
        "prompt": "12 bar blues in G",
        "expected_contains": ["G", "dom7", "C", "D"],
        "expected_any": [],
    },
    {
        "prompt": "sad chord progression in A minor, 4 bars",
        "expected_contains": ["A", "minor"],
        "expected_any": ["F", "C", "E", "Dm", "G"],
    },
    {
        "prompt": "jazz ii-V-I in Bb major",
        "expected_contains": ["Bb", "C", "min", "F"],
        "expected_any": ["dom7", "maj7", "min7"],
    },
    {
        "prompt": "I-V-vi-IV progression in G major",
        "expected_contains": ["G", "major", "D", "E", "minor", "C"],
        "expected_any": [],
    },
    {
        "prompt": "bossa nova chord progression in F major, 8 bars",
        "expected_contains": ["F"],
        "expected_any": ["Gm", "min", "C", "Am", "Dm", "Bb"],
    },
    {
        "prompt": "4 bar chord loop in D minor",
        "expected_contains": ["D", "minor"],
        "expected_any": ["A", "Bb", "C", "F", "G"],
    },
]


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


def generate(prompt, use_grammar=False):
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.3,
        "max_tokens": 512,
    }
    if use_grammar:
        payload["grammar"] = GBNF.strip()

    t0 = time.perf_counter()
    r = requests.post(f"{LLAMA_SERVER_URL}/v1/chat/completions", json=payload, timeout=120)
    wall_s = round(time.perf_counter() - t0, 3)
    r.raise_for_status()
    body = r.json()

    output = body["choices"][0]["message"]["content"].strip()
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
    return output, timing


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--grammar", action="store_true")
    args = parser.parse_args()

    if not wait_for_server():
        return

    mode = "GBNF-constrained" if args.grammar else "unconstrained"
    print()
    print("=" * 70)
    print(f"QWEN CHORD POC — Compact CHORD syntax [{mode}]")
    print("=" * 70)

    total_score = 0
    total_possible = 0
    total_elapsed = 0.0
    case_results = []

    for i, tc in enumerate(TEST_CASES, 1):
        prompt = tc["prompt"]
        expected_all = tc["expected_contains"]
        expected_any = tc["expected_any"]
        print(f"\n[{i}/{len(TEST_CASES)}] {prompt}")
        print("-" * 60)

        try:
            output, timing = generate(prompt, use_grammar=args.grammar)

            hits_all = sum(1 for e in expected_all if e in output)
            hit_any = 1 if not expected_any else (1 if any(e in output for e in expected_any) else 0)
            possible = len(expected_all) + (1 if expected_any else 0)
            score = hits_all + hit_any
            passed = score == possible

            total_score += score
            total_possible += possible
            total_elapsed += timing["wall_s"]

            tok_s = timing.get("tokens_per_sec", "?")
            out_display = output.replace("\n", "\n           ")
            print(f"  Output:  {out_display}")
            print(f"  Wall:    {timing['wall_s']:.2f}s  ({timing['completion_tokens']} tokens, {tok_s} tok/s)")
            print(f"  Score:   {score}/{possible} — {'PASS' if passed else 'FAIL'}")

            case_results.append({
                "prompt": prompt,
                "output": output,
                "timing": timing,
                "score": score,
                "possible": possible,
                "passed": passed,
            })

        except Exception as e:
            print(f"  ERROR: {e}")
            case_results.append({
                "prompt": prompt, "error": str(e),
                "score": 0, "possible": len(expected_all) + (1 if expected_any else 0), "passed": False,
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
        "model": "qwen2.5-coder-7b",
        "mode": f"chords_{mode}",
        "score_pct": pct,
        "avg_elapsed_s": round(avg, 3),
        "cases": case_results,
    }
    with open(RESULTS_FILE, "a") as f:
        f.write(json.dumps(run) + "\n")
    print(f"Results saved to {RESULTS_FILE}")


if __name__ == "__main__":
    main()
