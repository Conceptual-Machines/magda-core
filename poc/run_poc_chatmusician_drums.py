#!/usr/bin/env python3
"""POC: ChatMusician for drum/step pattern generation via ABC notation."""

import json
import time
from pathlib import Path

import requests

LLAMA_SERVER_URL = "http://127.0.0.1:8080"
RESULTS_FILE = Path(__file__).parent / "results" / "results_chatmusician_drums.jsonl"

SYSTEM_PROMPT = """You are a drum pattern generator. Generate drum patterns in ABC notation.
Use standard ABC percussion notation. Each line is one instrument.
Use x for hits, . for rests. 16 steps per bar (16th notes in 4/4).

Format:
%%MIDI channel 10
K: C
L: 1/16

Example 4/4 rock beat:
BD: x...x...x...x...
SD: ....x.......x...
HH: x.x.x.x.x.x.x.x.

Respond with ONLY the pattern. No prose."""

TEST_CASES = [
    {
        "prompt": "basic rock drum beat, 1 bar",
        "check": lambda out: any(k in out.lower() for k in ["kick", "bd", "snare", "sd", "hat", "hh", "x"]),
    },
    {
        "prompt": "hip hop drum pattern with kick and snare",
        "check": lambda out: any(k in out.lower() for k in ["kick", "bd", "snare", "sd", "x"]),
    },
    {
        "prompt": "simple 4 on the floor dance beat",
        "check": lambda out: any(k in out.lower() for k in ["kick", "bd", "x", "four"]),
    },
    {
        "prompt": "jazz swing ride pattern with kick and snare",
        "check": lambda out: any(k in out.lower() for k in ["ride", "swing", "kick", "bd", "x"]),
    },
    {
        "prompt": "bossa nova drum pattern",
        "check": lambda out: any(k in out.lower() for k in ["x", "kick", "bd", "rim", "hat", "hh"]),
    },
    {
        "prompt": "drum and bass breakbeat pattern",
        "check": lambda out: any(k in out.lower() for k in ["x", "kick", "bd", "snare", "sd", "break"]),
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


def generate(prompt):
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.3,
        "max_tokens": 256,
    }

    t0 = time.perf_counter()
    r = requests.post(f"{LLAMA_SERVER_URL}/v1/chat/completions", json=payload, timeout=60)
    wall_s = round(time.perf_counter() - t0, 3)
    r.raise_for_status()
    body = r.json()

    output = body["choices"][0]["message"]["content"].strip()
    t = body.get("timings", {})
    usage = body.get("usage", {})
    timing = {
        "wall_s": wall_s,
        "completion_tokens": usage.get("completion_tokens"),
        "tokens_per_sec": round(t["predicted_per_second"], 1) if t.get("predicted_per_second") else None,
    }
    return output, timing


def main():
    if not wait_for_server():
        return

    print()
    print("=" * 70)
    print("CHATMUSICIAN DRUMS POC — Pattern Generation")
    print("=" * 70)

    passed = 0
    total = len(TEST_CASES)
    total_elapsed = 0.0

    for i, tc in enumerate(TEST_CASES, 1):
        prompt = tc["prompt"]
        print(f"\n[{i}/{total}] {prompt}")
        print("-" * 60)

        try:
            output, timing = generate(prompt)
            ok = tc["check"](output)
            if ok:
                passed += 1
            total_elapsed += timing["wall_s"]

            out_display = output.replace("\n", "\n           ")
            print(f"  Output:  {out_display}")
            print(f"  Wall:    {timing['wall_s']:.2f}s  ({timing['completion_tokens']} tokens, {timing.get('tokens_per_sec', '?')} tok/s)")
            print(f"  Usable:  {'YES' if ok else 'NO'}")

        except Exception as e:
            print(f"  ERROR: {e}")

    avg = total_elapsed / total if total else 0
    print()
    print("=" * 70)
    print(f"USABLE: {passed}/{total} ({round(passed/total*100)}%)  |  avg latency: {avg:.2f}s")
    print("=" * 70)


if __name__ == "__main__":
    main()
