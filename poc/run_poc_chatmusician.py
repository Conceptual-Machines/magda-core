#!/usr/bin/env python3
"""POC: ChatMusician — test ABC notation output for chord progressions.

ChatMusician is a LLaMA2-7B fine-tuned on ABC notation + music theory.
We test whether it can generate usable chord progressions in ABC format.
"""

import argparse
import json
import time
from pathlib import Path

import requests

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
LLAMA_SERVER_URL = "http://127.0.0.1:8080"
MODEL_PATH = "/Volumes/External SSD/models/chatmusician/ChatMusician.Q4_K_M.gguf"
RESULTS_FILE = Path(__file__).parent / "results" / "results_chatmusician.jsonl"

# ---------------------------------------------------------------------------
# System prompt
# ---------------------------------------------------------------------------
SYSTEM_PROMPT = """You are a music theory expert. When asked for chord progressions, respond with ABC notation only.
Use standard ABC chord symbols in square brackets, e.g. [Am] [Dm] [G7] [C].
Include a time signature and key signature. Keep it concise — just the chords and rhythm, no melody unless asked.
No explanation, no prose. ABC notation only."""

# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------
TEST_CASES = [
    {
        "prompt": "Give me a simple chord progression in C major, 4 bars",
        "expected_contains": ["[C]", "[G]"],
        "expected_any": ["[F]", "[Am]", "[Dm]", "[Em]"],
    },
    {
        "prompt": "Give me a ii-V-I progression in C major",
        "expected_contains": ["[Dm]", "[G]", "[C]"],
        "expected_any": [],
    },
    {
        "prompt": "Generate a 12 bar blues in G",
        "expected_contains": ["[G]"],
        "expected_any": ["[C]", "[C7]", "[D]", "[D7]"],
    },
    {
        "prompt": "Give me a sad chord progression in A minor, 4 bars",
        "expected_contains": ["[Am]"],
        "expected_any": ["[Dm]", "[Em]", "[E]", "[F]", "[G]"],
    },
    {
        "prompt": "Generate a jazz ii-V-I in Bb major",
        "expected_contains": [],
        "expected_any": ["[Cm7]", "[Cm]", "[F7]", "[F]", "[Bb]", "[Bbmaj7]"],
    },
    {
        "prompt": "Give me a I-V-vi-IV progression in G major",
        "expected_contains": ["[G]", "[D]", "[Em]", "[C]"],
        "expected_any": [],
    },
    {
        "prompt": "Generate a bossa nova chord progression in F major",
        "expected_contains": ["[F]"],
        "expected_any": ["[Gm]", "[Gm7]", "[C7]", "[Am]", "[Am7]", "[Dm]", "[Dm7]", "[Bb]", "[Bbmaj7]"],
    },
    {
        "prompt": "Give me a 4 bar chord loop in D minor",
        "expected_contains": ["[Dm]"],
        "expected_any": ["[Am]", "[Gm]", "[Bb]", "[C]", "[F]", "[A]", "[A7]"],
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


def generate(prompt: str) -> tuple[str, dict]:
    """Return (abc_output, timing)."""
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.3,
        "max_tokens": 512,
    }

    t0 = time.perf_counter()
    r = requests.post(f"{LLAMA_SERVER_URL}/v1/chat/completions", json=payload, timeout=120)
    wall_s = round(time.perf_counter() - t0, 3)

    r.raise_for_status()
    body = r.json()

    abc = body["choices"][0]["message"]["content"].strip()

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
    return abc, timing


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not wait_for_server():
        return

    print()
    print("=" * 70)
    print("CHATMUSICIAN POC — Chord Progression via ABC Notation")
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
            abc, timing = generate(prompt)

            # Score: all required + at least one from any
            hits_all = sum(1 for e in expected_all if e in abc)
            hit_any = 1 if not expected_any else (1 if any(e in abc for e in expected_any) else 0)
            possible = len(expected_all) + (1 if expected_any else 0)
            score = hits_all + hit_any
            passed = score == possible

            total_score += score
            total_possible += possible
            total_elapsed += timing["wall_s"]

            tok_s = timing.get("tokens_per_sec", "?")
            # Show ABC output (truncate long outputs)
            abc_display = abc.replace("\n", "\n           ")
            print(f"  ABC:     {abc_display}")
            print(f"  Wall:    {timing['wall_s']:.2f}s  ({timing['completion_tokens']} tokens, {tok_s} tok/s)")
            print(f"  Score:   {score}/{possible} — {'PASS' if passed else 'FAIL'}")

            case_results.append({
                "prompt": prompt,
                "abc": abc,
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
                "possible": len(expected_all) + (1 if expected_any else 0),
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
        "model": "ChatMusician-Q4_K_M",
        "mode": "abc_notation",
        "score_pct": pct,
        "avg_elapsed_s": round(avg, 3),
        "cases": case_results,
    }
    with open(RESULTS_FILE, "a") as f:
        f.write(json.dumps(run) + "\n")
    print(f"Results saved to {RESULTS_FILE}")


if __name__ == "__main__":
    main()
