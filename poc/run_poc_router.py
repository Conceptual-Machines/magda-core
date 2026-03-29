#!/usr/bin/env python3
"""POC: Local LLM as intent router — classifies user input to an agent.

The router outputs a single token: COMMAND, MUSIC, or CREATIVE.
Tests classification accuracy and latency.
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
MODELS = {
    "32b":  "/Volumes/External SSD/models/qwen2.5-coder-32b/Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf",
    "7b":   "/Volumes/External SSD/models/qwen2.5-coder-7b/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
    "3b":   "/Volumes/External SSD/models/qwen2.5-coder-3b/Qwen2.5-Coder-3B-Instruct-Q4_K_M.gguf",
    "1.5b": "/Volumes/External SSD/models/qwen2.5-coder-1.5b/Qwen2.5-Coder-1.5B-Instruct-Q4_K_M.gguf",
}
RESULTS_FILE = Path(__file__).parent / "results" / "results_router.jsonl"

# ---------------------------------------------------------------------------
# GBNF grammar — force single token output
# ---------------------------------------------------------------------------
ROUTER_GBNF = r"""
root ::= agent
agent ::= "COMMAND" | "MUSIC"
"""

# ---------------------------------------------------------------------------
# System prompt
# ---------------------------------------------------------------------------
SYSTEM_PROMPT = """You are a router for a DAW AI assistant. Classify the user's request into exactly one agent.

COMMAND — Modifying or creating project elements: create/delete/rename tracks, add/move/duplicate/delete clips, add FX (reverb, EQ, compressor), set volume/pan/mute/solo, quantize/transpose notes, set tempo.
Examples: "create a bass track", "delete track 2", "add reverb to vocals", "mute the drums", "quantize to 1/16", "add a 4 bar clip on track 1", "set volume to -6 dB"

MUSIC — Generating musical content: suggest/generate chord progressions, suggest chords, harmonize melodies, generate chord loops.
Examples: "suggest chords in D minor", "give me a jazz ii-V-I", "generate a blues progression", "harmonize this melody"

Respond with ONLY: COMMAND or MUSIC."""

# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------
TEST_CASES = [
    # COMMAND cases
    {"prompt": "create a bass track", "expected": "COMMAND"},
    {"prompt": "delete track 2", "expected": "COMMAND"},
    {"prompt": "mute all tracks named Drums", "expected": "COMMAND"},
    {"prompt": "add reverb and EQ to the Vocals track", "expected": "COMMAND"},
    {"prompt": "set volume of track 3 to -6 dB and pan it left", "expected": "COMMAND"},
    {"prompt": "add a 4 bar clip at bar 1 on track 1", "expected": "COMMAND"},
    {"prompt": "quantize the notes to 1/16", "expected": "COMMAND"},
    {"prompt": "transpose track 2 up by 3 semitones", "expected": "COMMAND"},
    {"prompt": "solo the guitar track", "expected": "COMMAND"},
    {"prompt": "duplicate the clip on track 4", "expected": "COMMAND"},
    {"prompt": "rename track 1 to Lead Synth", "expected": "COMMAND"},
    {"prompt": "add a compressor to the master", "expected": "COMMAND"},
    {"prompt": "move the clip to bar 5", "expected": "COMMAND"},
    {"prompt": "add a 2 bar clip with a C major arpeggio on track 1", "expected": "COMMAND"},
    {"prompt": "set the tempo to 120 bpm", "expected": "COMMAND"},

    # MUSIC cases
    {"prompt": "suggest a chord progression in D minor", "expected": "MUSIC"},
    {"prompt": "suggest a chord after Am7", "expected": "MUSIC"},
    {"prompt": "give me a jazzy ii-V-I in Bb", "expected": "MUSIC"},
    {"prompt": "suggest some chords that fit E dorian", "expected": "MUSIC"},
    {"prompt": "give me a 4 bar chord loop in A minor", "expected": "MUSIC"},
    {"prompt": "suggest a chord substitution for G7", "expected": "MUSIC"},
    {"prompt": "harmonize this melody: E F# G A B", "expected": "MUSIC"},
    {"prompt": "generate a sad chord progression", "expected": "MUSIC"},
    {"prompt": "give me some gospel chords in C", "expected": "MUSIC"},
    {"prompt": "suggest chords for a blues in G", "expected": "MUSIC"},
    {"prompt": "generate a neo-soul progression", "expected": "MUSIC"},
    {"prompt": "give me a 12 bar blues", "expected": "MUSIC"},
    {"prompt": "suggest a turnaround in Bb major", "expected": "MUSIC"},
    {"prompt": "generate chords for a dreamy ambient pad", "expected": "MUSIC"},
    {"prompt": "give me a bossa nova progression in F", "expected": "MUSIC"},
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


def classify(prompt: str, use_grammar: bool = False) -> tuple[str, dict]:
    """Return (agent_name, timing)."""
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.0,
        "max_tokens": 4,
    }
    if use_grammar:
        payload["grammar"] = ROUTER_GBNF.strip()

    t0 = time.perf_counter()
    r = requests.post(f"{LLAMA_SERVER_URL}/v1/chat/completions", json=payload, timeout=30)
    wall_s = round(time.perf_counter() - t0, 3)

    r.raise_for_status()
    body = r.json()

    result = body["choices"][0]["message"]["content"].strip().upper()

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
    return result, timing


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
    print(f"MAGDA ROUTER POC — {model_label} [{mode}]")
    print("=" * 70)

    correct = 0
    total = len(TEST_CASES)
    total_elapsed = 0.0
    case_results = []

    # Group by expected agent for display
    for agent in ["COMMAND", "MUSIC"]:
        cases = [tc for tc in TEST_CASES if tc["expected"] == agent]
        print(f"\n--- {agent} ({len(cases)} cases) ---")

        for tc in cases:
            prompt = tc["prompt"]
            expected = tc["expected"]

            try:
                result, timing = classify(prompt, use_grammar=args.grammar)
                passed = result == expected
                if passed:
                    correct += 1
                total_elapsed += timing["wall_s"]

                status = "OK" if passed else f"WRONG → {result}"
                print(f"  [{timing['wall_s']:.2f}s] {status:12s}  {prompt}")

                case_results.append({
                    "prompt": prompt,
                    "expected": expected,
                    "result": result,
                    "passed": passed,
                    "timing": timing,
                })

            except Exception as e:
                print(f"  [ERROR]  {prompt}: {e}")
                case_results.append({
                    "prompt": prompt,
                    "expected": expected,
                    "error": str(e),
                    "passed": False,
                })

    avg = total_elapsed / total if total else 0
    pct = round(correct / total * 100)

    print()
    print("=" * 70)
    print(f"ACCURACY: {correct}/{total} ({pct}%)  |  avg latency: {avg:.3f}s")
    print("=" * 70)

    # Per-agent breakdown
    for agent in ["COMMAND", "MUSIC"]:
        agent_cases = [c for c in case_results if c["expected"] == agent]
        agent_correct = sum(1 for c in agent_cases if c.get("passed"))
        agent_avg = sum(c.get("timing", {}).get("wall_s", 0) for c in agent_cases) / len(agent_cases) if agent_cases else 0
        print(f"  {agent:10s}  {agent_correct}/{len(agent_cases)}  avg {agent_avg:.3f}s")

    # Save results
    RESULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    run = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "model": model_label,
        "mode": f"router_{mode}",
        "accuracy_pct": pct,
        "avg_elapsed_s": round(avg, 3),
        "cases": case_results,
    }
    with open(RESULTS_FILE, "a") as f:
        f.write(json.dumps(run) + "\n")
    print(f"\nResults saved to {RESULTS_FILE}")


if __name__ == "__main__":
    main()
