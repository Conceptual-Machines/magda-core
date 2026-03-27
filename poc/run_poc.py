"""
MAGDA DSL POC — local LLM via llama-server
Tests how well a local model generates MAGDA DSL commands.

Usage:
  .venv/bin/python3 run_poc.py            # 32B, no grammar constraints
  .venv/bin/python3 run_poc.py --7b       # 7B model
  .venv/bin/python3 run_poc.py --grammar  # GBNF-constrained output

Results are appended to results/results.jsonl (one JSON object per run).

llama-server must be running:
  llama-server -m ~/models/qwen2.5-coder-32b/Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf \
               --port 8080 -ngl 99 -c 8192
"""

import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import requests

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
LLAMA_SERVER_URL = "http://127.0.0.1:8080"
MODELS = {
    "32b": "~/models/qwen2.5-coder-32b/Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf",
    "7b":  "~/models/qwen2.5-coder-7b/Qwen2.5-Coder-7B-Instruct-Q4_K_M.gguf",
}
RESULTS_FILE = Path(__file__).parent / "results" / "results.jsonl"

# ---------------------------------------------------------------------------
# GBNF grammar — mechanical conversion of the Lark grammar in dsl_grammar.hpp
# Passed to llama-server as the "grammar" field to constrain output syntax.
# ---------------------------------------------------------------------------
GBNF = r"""
root ::= ws? statement (ws? statement)* ws?
statement ::= track-statement | filter-statement
track-statement ::= "track" ws? "(" ws? params? ws? ")" (ws? chain)?
filter-statement ::= "filter" ws? "(" ws? "tracks" ws? "," ws? condition ws? ")" (ws? chain)?
condition ::= "track" ws? "." ws? identifier ws? "==" ws? value
clip-condition ::= "clip" ws? "." ws? identifier ws? compare-op ws? value
note-condition ::= "note" ws? "." ws? identifier ws? compare-op ws? value
compare-op ::= "==" | "!=" | ">=" | "<=" | ">" | "<"
chain ::= method+
method ::= ws? "." method-call
method-call ::= "clip" "." "new" "(" ws? params? ws? ")"
             | "clip" "." "rename" "(" ws? params? ws? ")"
             | "clip" "." "delete" "(" ws? params? ws? ")"
             | "clips" "." "select" "(" ws? clip-condition ws? ")"
             | "track" "." "set" "(" ws? params? ws? ")"
             | "fx" "." "add" "(" ws? params? ws? ")"
             | "notes" "." "add" "(" ws? params? ws? ")"
             | "notes" "." "add_chord" "(" ws? params? ws? ")"
             | "notes" "." "add_arpeggio" "(" ws? params? ws? ")"
             | "notes" "." "select" "(" ws? note-condition ws? ")"
             | "notes" "." "delete" "(" ws? ")"
             | "notes" "." "transpose" "(" ws? params? ws? ")"
             | "notes" "." "set_pitch" "(" ws? params? ws? ")"
             | "notes" "." "set_velocity" "(" ws? params? ws? ")"
             | "notes" "." "quantize" "(" ws? params? ws? ")"
             | "notes" "." "resize" "(" ws? params? ws? ")"
             | "delete" "(" ws? ")"
             | "select" "(" ws? ")"
             | "for_each" "(" ws? chain ws? ")"
params ::= param (ws? "," ws? param)*
param ::= identifier ws? "=" ws? value
value ::= string | number | boolean | identifier | array
array ::= "[" ws? array-items? ws? "]"
array-items ::= identifier (ws? "," ws? identifier)*
string ::= "\"" [^"]* "\""
number ::= "-"? [0-9]+ ("." [0-9]+)?
boolean ::= "true" | "false" | "True" | "False"
identifier ::= [a-zA-Z_#] [a-zA-Z0-9_#]*
ws ::= [ \t\n\r]+
"""

SYSTEM_PROMPT = """You are MAGDA, an AI assistant for a DAW (Digital Audio Workstation).
You MUST respond with ONLY valid MAGDA DSL code. No explanation, no markdown, no prose.

TRACK OPERATIONS:
- track() - Create new track
- track(name="Bass") - Create or reference track by name
- track(name="Bass", new=true) - Always create a new track
- track(id=1) - Reference existing track (1-based index)

METHOD CHAINING:
- .clip.new(bar=3, length_bars=4) - Create MIDI clip at bar
- .clip.new(length_bars=4) - Create MIDI clip after last clip
- .track.set(name="X", volume_db=-3, pan=0.5, mute=true, solo=true)
- .fx.add(name="eq") - Add FX (eq, compressor, reverb, delay, chorus, phaser, filter, utility)
- .fx.add(name="Pro-Q 3") - Add plugin by name
- .delete() - Delete track
- .select() - Select track

FILTER OPERATIONS:
- filter(tracks, track.name == "X").delete()
- filter(tracks, track.name == "X").track.set(mute=true)
- filter(tracks, track.name == "X").for_each(.clip.new(bar=1, length_bars=4))

NOTE OPERATIONS:
- .notes.add(pitch=C4, beat=0, length=1, velocity=100)
- .notes.add_chord(root=C4, quality=major, beat=0, length=1, velocity=100, inversion=0)
- .notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, fill=true, pattern=up)
- .notes.select(note.pitch == C4)
- .notes.delete()
- .notes.transpose(semitones=5)
- .notes.set_velocity(value=80)
- .notes.quantize(grid=0.25)
- .notes.resize(length=0.5)

EXAMPLES:
"create a bass track" -> track(name="Bass", new=true)
"delete track 1" -> track(id=1).delete()
"mute all tracks named Drums" -> filter(tracks, track.name == "Drums").track.set(mute=true)
"add a 4 bar clip at bar 1 on track 2" -> track(id=2).clip.new(bar=1, length_bars=4)
"add reverb to Vocals" -> track(name="Vocals").fx.add(name="reverb")
"add a C major arpeggio" -> track(id=1).notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5)
"add E minor to C major progression over 4 bars" ->
  track(id=1).clip.new(length_bars=4).notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, beats=8).notes.add_arpeggio(root=C4, quality=major, beat=8, step=0.5, beats=8)

Respond with DSL only. No markdown fences. No explanation."""

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
        "expected_contains": ["clip.new(", "notes.add_arpeggio(", "C4", "major"],
    },
    {
        "prompt": "set volume of track 3 to -6 dB and pan it left",
        "expected_contains": ["track(id=3)", "track.set(", "volume_db=-6"],
    },
    {
        "prompt": "add an E minor to C major chord progression over 4 bars on track 1",
        "expected_contains": ["clip.new(", "add_arpeggio(", "E4", "min", "C4", "major"],
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
        print(".", end="", flush=True)
        time.sleep(1)
    print(" timed out.")
    return False


def generate_dsl(prompt: str, use_grammar: bool = False) -> tuple[str, float]:
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.1,
        "max_tokens": 512,
    }

    if use_grammar:
        payload["grammar"] = GBNF.strip()

    t0 = time.perf_counter()
    r = requests.post(
        f"{LLAMA_SERVER_URL}/v1/chat/completions",
        json=payload,
        timeout=60,
    )
    elapsed = time.perf_counter() - t0

    r.raise_for_status()
    return r.json()["choices"][0]["message"]["content"].strip(), elapsed


def score(dsl: str, expected_contains: list[str]) -> tuple[int, int]:
    hits = sum(1 for token in expected_contains if token.lower() in dsl.lower())
    return hits, len(expected_contains)


def save_results(run: dict):
    RESULTS_FILE.parent.mkdir(exist_ok=True)
    with RESULTS_FILE.open("a") as f:
        f.write(json.dumps(run) + "\n")


def run_tests(model_key: str = "32b", use_grammar: bool = False):
    mode = "GBNF-constrained" if use_grammar else "unconstrained"
    print("\n" + "=" * 70)
    print(f"MAGDA DSL POC — Qwen2.5-Coder-{model_key.upper()}-Instruct Q4_K_M [{mode}]")
    print("=" * 70)

    run = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "model": f"Qwen2.5-Coder-{model_key.upper()}-Instruct-Q4_K_M",
        "grammar_constrained": use_grammar,
        "cases": [],
    }

    total_hits = 0
    total_checks = 0

    for i, tc in enumerate(TEST_CASES, 1):
        print(f"\n[{i}/{len(TEST_CASES)}] {tc['prompt']}")
        print("-" * 60)

        try:
            dsl, elapsed = generate_dsl(tc["prompt"], use_grammar=use_grammar)
        except Exception as e:
            print(f"  ERROR: {e}")
            run["cases"].append({"prompt": tc["prompt"], "error": str(e)})
            continue

        hits, checks = score(dsl, tc["expected_contains"])
        total_hits += hits
        total_checks += checks
        status = "PASS" if hits == checks else "PARTIAL" if hits > 0 else "FAIL"
        missing = [t for t in tc["expected_contains"] if t.lower() not in dsl.lower()]

        print(f"  DSL:     {dsl}")
        print(f"  Time:    {elapsed:.2f}s")
        print(f"  Score:   {hits}/{checks} — {status}")
        if missing:
            print(f"  Missing: {missing}")

        run["cases"].append({
            "prompt": tc["prompt"],
            "dsl": dsl,
            "elapsed_s": round(elapsed, 3),
            "hits": hits,
            "checks": checks,
            "status": status,
            "missing": missing,
        })

    pct = int(100 * total_hits / total_checks) if total_checks else 0
    avg_time = sum(c["elapsed_s"] for c in run["cases"] if "elapsed_s" in c) / len(run["cases"])

    run["total_hits"] = total_hits
    run["total_checks"] = total_checks
    run["score_pct"] = pct
    run["avg_elapsed_s"] = round(avg_time, 3)

    print("\n" + "=" * 70)
    print(f"TOTAL: {total_hits}/{total_checks} ({pct}%)  |  avg latency: {avg_time:.2f}s")
    print("=" * 70)

    save_results(run)
    print(f"Results saved to {RESULTS_FILE}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    use_grammar = "--grammar" in sys.argv
    model_key = next((a.lstrip("-") for a in sys.argv[1:] if a.lstrip("-") in MODELS), "32b")

    if not wait_for_server(timeout=10):
        model_path = MODELS[model_key]
        print("\nllama-server is not running. Start it with:")
        print(f"\n  llama-server -m {model_path} --port 8080 -ngl 99 -c 8192\n")
        sys.exit(1)

    run_tests(model_key=model_key, use_grammar=use_grammar)
