#!/usr/bin/env python3
"""POC: Local LLM DSL generation via function/tool calling.

Instead of asking the model to produce raw DSL text, we define tools
that map to DSL actions and let the model call them.  We then stitch
the tool calls back into valid DSL.
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
RESULTS_FILE = Path(__file__).parent / "results" / "results_tools.jsonl"

# ---------------------------------------------------------------------------
# Tool definitions (OpenAI function-calling format)
# ---------------------------------------------------------------------------
TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "create_track",
            "description": "Create a new track in the DAW",
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Track name"},
                },
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "delete_track",
            "description": "Delete a track by its 1-based index",
            "parameters": {
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "Track index (1-based)"},
                },
                "required": ["id"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "set_track",
            "description": "Set properties on a track (volume, pan, mute, solo, name)",
            "parameters": {
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "Track index (1-based)"},
                    "name": {"type": "string", "description": "Track name to match (use instead of id to match by name)"},
                    "volume_db": {"type": "number", "description": "Volume in dB"},
                    "pan": {"type": "number", "description": "Pan (-1 left, 0 center, 1 right)"},
                    "mute": {"type": "boolean", "description": "Mute state"},
                    "solo": {"type": "boolean", "description": "Solo state"},
                },
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "filter_tracks_set",
            "description": "Find tracks matching a condition and set properties on them",
            "parameters": {
                "type": "object",
                "properties": {
                    "match_name": {"type": "string", "description": "Track name to match"},
                    "mute": {"type": "boolean"},
                    "solo": {"type": "boolean"},
                    "volume_db": {"type": "number"},
                    "pan": {"type": "number"},
                },
                "required": ["match_name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "add_clip",
            "description": "Add a MIDI clip to a track",
            "parameters": {
                "type": "object",
                "properties": {
                    "track_id": {"type": "integer", "description": "Track index (1-based)"},
                    "track_name": {"type": "string", "description": "Track name (use instead of track_id)"},
                    "bar": {"type": "integer", "description": "Starting bar (1-based)"},
                    "length_bars": {"type": "integer", "description": "Clip length in bars"},
                },
                "required": ["length_bars"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "add_fx",
            "description": "Add an effect/plugin to a track",
            "parameters": {
                "type": "object",
                "properties": {
                    "track_id": {"type": "integer", "description": "Track index (1-based)"},
                    "track_name": {"type": "string", "description": "Track name"},
                    "fx_name": {"type": "string", "description": "Effect name (eq, compressor, reverb, delay, chorus, phaser, filter, utility, or plugin name)"},
                },
                "required": ["fx_name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "add_notes",
            "description": "Add individual notes to the current clip on a track",
            "parameters": {
                "type": "object",
                "properties": {
                    "track_id": {"type": "integer", "description": "Track index (1-based)"},
                    "track_name": {"type": "string", "description": "Track name"},
                    "notes": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "pitch": {"type": "string", "description": "Note pitch e.g. C4, D#5"},
                                "beat": {"type": "number"},
                                "length": {"type": "number"},
                                "velocity": {"type": "integer"},
                            },
                            "required": ["pitch", "beat", "length"],
                        },
                    },
                },
                "required": ["notes"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "add_arpeggio",
            "description": "Add an arpeggio pattern to a clip on a track",
            "parameters": {
                "type": "object",
                "properties": {
                    "track_id": {"type": "integer", "description": "Track index (1-based)"},
                    "track_name": {"type": "string", "description": "Track name"},
                    "root": {"type": "string", "description": "Root note e.g. C4, E4"},
                    "quality": {"type": "string", "enum": ["major", "minor", "min", "dim", "aug", "sus2", "sus4"]},
                    "beat": {"type": "number", "description": "Starting beat"},
                    "step": {"type": "number", "description": "Step size in beats between notes"},
                    "beats": {"type": "number", "description": "Total duration in beats"},
                    "pattern": {"type": "string", "enum": ["up", "down", "updown"]},
                },
                "required": ["root", "quality", "beat", "step"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "add_chord",
            "description": "Add a chord to a clip on a track",
            "parameters": {
                "type": "object",
                "properties": {
                    "track_id": {"type": "integer", "description": "Track index (1-based)"},
                    "track_name": {"type": "string", "description": "Track name"},
                    "root": {"type": "string", "description": "Root note e.g. C4"},
                    "quality": {"type": "string", "enum": ["major", "minor", "min", "dim", "aug", "sus2", "sus4"]},
                    "beat": {"type": "number"},
                    "length": {"type": "number"},
                    "velocity": {"type": "integer"},
                    "inversion": {"type": "integer"},
                },
                "required": ["root", "quality", "beat", "length"],
            },
        },
    },
]

# ---------------------------------------------------------------------------
# Tool call → DSL reconstruction
# ---------------------------------------------------------------------------
def tool_call_to_dsl(name: str, args: dict) -> str:
    """Convert a tool call back into MAGDA DSL."""
    if name == "create_track":
        return f'track(name="{args["name"]}", new=true)'

    if name == "delete_track":
        return f'track(id={args["id"]}).delete()'

    if name == "set_track":
        target = f'track(id={args["id"]})' if "id" in args else f'track(name="{args["name"]}")'
        props = {k: v for k, v in args.items() if k not in ("id", "name")}
        prop_str = ", ".join(f"{k}={json.dumps(v) if isinstance(v, str) else str(v).lower() if isinstance(v, bool) else v}" for k, v in props.items())
        return f'{target}.track.set({prop_str})'

    if name == "filter_tracks_set":
        match = args["match_name"]
        props = {k: v for k, v in args.items() if k != "match_name"}
        prop_str = ", ".join(f"{k}={json.dumps(v) if isinstance(v, str) else str(v).lower() if isinstance(v, bool) else v}" for k, v in props.items())
        return f'filter(tracks, track.name == "{match}").track.set({prop_str})'

    if name == "add_clip":
        target = f'track(id={args["track_id"]})' if "track_id" in args else f'track(name="{args["track_name"]}")'
        clip_args = {k: v for k, v in args.items() if k not in ("track_id", "track_name")}
        clip_str = ", ".join(f"{k}={v}" for k, v in clip_args.items())
        return f'{target}.clip.new({clip_str})'

    if name == "add_fx":
        target = f'track(id={args["track_id"]})' if "track_id" in args else f'track(name="{args["track_name"]}")'
        return f'{target}.fx.add(name="{args["fx_name"]}")'

    if name == "add_arpeggio":
        target = f'track(id={args["track_id"]})' if "track_id" in args else f'track(name="{args.get("track_name", "")}")'
        arp_args = {k: v for k, v in args.items() if k not in ("track_id", "track_name")}
        arp_str = ", ".join(f"{k}={v}" for k, v in arp_args.items())
        return f'{target}.notes.add_arpeggio({arp_str})'

    if name == "add_chord":
        target = f'track(id={args["track_id"]})' if "track_id" in args else f'track(name="{args.get("track_name", "")}")'
        chord_args = {k: v for k, v in args.items() if k not in ("track_id", "track_name")}
        chord_str = ", ".join(f"{k}={v}" for k, v in chord_args.items())
        return f'{target}.notes.add_chord({chord_str})'

    if name == "add_notes":
        target = f'track(id={args["track_id"]})' if "track_id" in args else f'track(name="{args.get("track_name", "")}")'
        lines = []
        for n in args["notes"]:
            note_str = ", ".join(f"{k}={v}" for k, v in n.items())
            lines.append(f'{target}.notes.add({note_str})')
        return "\n".join(lines)

    return f"UNKNOWN_TOOL({name}, {args})"


def reconstruct_dsl(tool_calls: list[dict]) -> str:
    """Convert a list of tool calls into DSL lines."""
    lines = []
    for tc in tool_calls:
        fn = tc["function"]
        name = fn["name"]
        args = json.loads(fn["arguments"]) if isinstance(fn["arguments"], str) else fn["arguments"]
        lines.append(tool_call_to_dsl(name, args))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# System prompt
# ---------------------------------------------------------------------------
SYSTEM_PROMPT = """You are MAGDA, an AI assistant for a DAW (Digital Audio Workstation).
Use the provided tools to execute the user's request. Call multiple tools if needed.
For chord progressions, call add_arpeggio multiple times with different beat offsets.
Always use the most specific tool available."""

# ---------------------------------------------------------------------------
# Test cases — same prompts, but expected_contains checks the reconstructed DSL
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
        "expected_contains": ["track(id=3)", "set(", "volume_db=-6"],
    },
    {
        "prompt": "add an E minor to C major chord progression over 4 bars on track 1",
        "expected_contains": ["add_arpeggio(", "E4", "min", "C4", "major"],
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


def generate_tool_calls(prompt: str) -> tuple[str, list[dict], dict]:
    """Send prompt with tools, return (reconstructed_dsl, raw_tool_calls, timing)."""
    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "tools": TOOLS,
        "tool_choice": "required",
        "temperature": 0.1,
        "max_tokens": 512,
    }

    t0 = time.perf_counter()
    r = requests.post(
        f"{LLAMA_SERVER_URL}/v1/chat/completions",
        json=payload,
        timeout=60,
    )
    wall_s = round(time.perf_counter() - t0, 3)

    r.raise_for_status()
    body = r.json()

    msg = body["choices"][0]["message"]
    tool_calls = msg.get("tool_calls", [])

    if tool_calls:
        dsl = reconstruct_dsl(tool_calls)
    else:
        # Fallback: model replied with text instead of tool calls
        dsl = msg.get("content", "").strip()

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

    return dsl, tool_calls, timing


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--32b", dest="model", action="store_const", const="32b")
    parser.add_argument("--7b",  dest="model", action="store_const", const="7b")
    parser.add_argument("--3b",  dest="model", action="store_const", const="3b")
    parser.add_argument("--1.5b", dest="model", action="store_const", const="1.5b")
    args = parser.parse_args()
    model_key = args.model or "7b"
    model_path = MODELS[model_key]

    if not wait_for_server():
        return

    model_label = Path(model_path).stem
    print()
    print("=" * 70)
    print(f"MAGDA DSL POC (TOOL CALLING) — {model_label}")
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
            dsl, tool_calls, timing = generate_tool_calls(prompt)

            # Score
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
            print(f"  Tools:   {len(tool_calls)} call(s): {', '.join(tc['function']['name'] for tc in tool_calls)}")
            print(f"  DSL:     {dsl}")
            print(f"  Wall:    {timing['wall_s']:.2f}s  (prompt {p_ms}ms  gen {g_ms}ms  {tok_s} tok/s)")
            print(f"  Tokens:  prompt={timing['prompt_tokens']}  completion={timing['completion_tokens']}")
            print(f"  Score:   {score}/{possible} — {'PASS' if passed else 'FAIL'}")

            case_results.append({
                "prompt": prompt,
                "dsl": dsl,
                "tool_calls": [{"name": tc["function"]["name"], "args": tc["function"]["arguments"]} for tc in tool_calls],
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

    # Save results
    RESULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    run = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "model": model_label,
        "mode": "tool_calling",
        "score_pct": pct,
        "avg_elapsed_s": round(avg, 3),
        "cases": case_results,
    }
    with open(RESULTS_FILE, "a") as f:
        f.write(json.dumps(run) + "\n")
    print(f"Results saved to {RESULTS_FILE}")


if __name__ == "__main__":
    main()
