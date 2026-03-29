#!/usr/bin/env python3
"""POC: Cloud model benchmark — test all agents across multiple providers.

Tests: router, commands (compact DSL), chord suggestions (continuation + mood).
Providers: OpenAI, Anthropic, Google Gemini, DeepSeek, OpenRouter.
All providers run in parallel using async httpx.
"""

import asyncio
import json
import os
import time
from pathlib import Path

import httpx

# Load .env
env_path = Path(__file__).parent.parent / ".env"
if env_path.exists():
    for line in env_path.read_text().splitlines():
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip())

RESULTS_FILE = Path(__file__).parent / "results" / "results_cloud.jsonl"

# ---------------------------------------------------------------------------
# Provider configs
# ---------------------------------------------------------------------------
PROVIDERS = {
    "gpt-5": {
        "type": "openai_responses",
        "url": "https://api.openai.com/v1/responses",
        "model": "gpt-5",
        "no_temperature": True,
        "reasoning_effort": "minimal",
        "headers": lambda: {"Authorization": f"Bearer {os.environ['OPENAI_API_KEY']}"},
    },
    "claude-opus": {
        "type": "anthropic",
        "url": "https://api.anthropic.com/v1/messages",
        "model": "claude-opus-4-20250514",
        "headers": lambda: {
            "x-api-key": os.environ["ANTHROPIC_API_KEY"],
            "anthropic-version": "2023-06-01",
        },
    },
    "claude-sonnet-4.6": {
        "type": "anthropic",
        "url": "https://api.anthropic.com/v1/messages",
        "model": "claude-sonnet-4-6",
        "headers": lambda: {
            "x-api-key": os.environ["ANTHROPIC_API_KEY"],
            "anthropic-version": "2023-06-01",
        },
    },
    "claude-haiku": {
        "type": "anthropic",
        "url": "https://api.anthropic.com/v1/messages",
        "model": "claude-haiku-4-5-20251001",
        "headers": lambda: {
            "x-api-key": os.environ["ANTHROPIC_API_KEY"],
            "anthropic-version": "2023-06-01",
        },
    },
    "gpt-4.1": {
        "url": "https://api.openai.com/v1/chat/completions",
        "model": "gpt-4.1",
        "headers": lambda: {"Authorization": f"Bearer {os.environ['OPENAI_API_KEY']}"},
    },
    "gemini-2.5-flash": {
        "type": "gemini",
        "model": "gemini-2.5-flash",
    },
    "deepseek-chat": {
        "url": "https://api.deepseek.com/v1/chat/completions",
        "model": "deepseek-chat",
        "headers": lambda: {"Authorization": f"Bearer {os.environ['DEEPSEEK_API_KEY']}"},
    },
    # --- OpenRouter models ---
    "or/llama-3.3-70b": {
        "type": "openrouter",
        "model": "meta-llama/llama-3.3-70b-instruct",
    },
    "or/qwen3-30b": {
        "type": "openrouter",
        "model": "qwen/qwen3-30b-a3b-04-28",
    },
    "or/deepseek-v3.1": {
        "type": "openrouter",
        "model": "deepseek/deepseek-chat-v3.1",
    },
    "or/mistral-small-3.1": {
        "type": "openrouter",
        "model": "mistralai/mistral-small-3.1-24b-instruct",
    },
    "or/gemini-2.5-pro": {
        "type": "openrouter",
        "model": "google/gemini-2.5-pro",
    },
    # --- Local models ---
    "local/qwen-7b": {
        "url": "http://127.0.0.1:8080/v1/chat/completions",
        "model": "local",
        "headers": lambda: {},
    },
}

# ---------------------------------------------------------------------------
# Async LLM call
# ---------------------------------------------------------------------------
async def call_llm(client: httpx.AsyncClient, provider_key: str, system: str, user: str) -> tuple[str, float]:
    """Returns (response_text, wall_seconds)."""
    cfg = PROVIDERS[provider_key]
    ptype = cfg.get("type", "openai_chat")

    if ptype == "anthropic":
        payload = {
            "model": cfg["model"],
            "max_tokens": 256,
            "temperature": 0.1,
            "system": system,
            "messages": [{"role": "user", "content": user}],
        }
        t0 = time.perf_counter()
        r = await client.post(
            cfg["url"],
            headers={**cfg["headers"](), "Content-Type": "application/json"},
            json=payload,
        )
        wall = round(time.perf_counter() - t0, 3)
        r.raise_for_status()
        text = r.json()["content"][0]["text"].strip()

    elif ptype == "openai_responses":
        payload = {
            "model": cfg["model"],
            "instructions": system,
            "input": user,
        }
        if not cfg.get("no_temperature"):
            payload["temperature"] = 0.1
        if cfg.get("reasoning_effort"):
            payload["reasoning"] = {"effort": cfg["reasoning_effort"]}
        t0 = time.perf_counter()
        r = await client.post(
            cfg["url"],
            headers={**cfg["headers"](), "Content-Type": "application/json"},
            json=payload,
        )
        wall = round(time.perf_counter() - t0, 3)
        r.raise_for_status()
        body = r.json()
        text = ""
        for item in body.get("output", []):
            if item.get("type") == "message":
                for content in item.get("content", []):
                    if content.get("type") == "output_text":
                        text = content["text"].strip()
                        break
        if not text:
            text = body["output"][0]["content"][0]["text"].strip()

    elif ptype == "gemini":
        url = f"https://generativelanguage.googleapis.com/v1beta/models/{cfg['model']}:generateContent?key={os.environ['GEMINI_API_KEY']}"
        payload = {
            "system_instruction": {"parts": [{"text": system}]},
            "contents": [{"parts": [{"text": user}]}],
            "generationConfig": {"temperature": 0.1},
        }
        t0 = time.perf_counter()
        r = await client.post(url, json=payload)
        wall = round(time.perf_counter() - t0, 3)
        r.raise_for_status()
        text = r.json()["candidates"][0]["content"]["parts"][0]["text"].strip()

    elif ptype == "openrouter":
        payload = {
            "model": cfg["model"],
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            "temperature": 0.1,
        }
        t0 = time.perf_counter()
        r = await client.post(
            "https://openrouter.ai/api/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {os.environ['OPENROUTER_API_KEY']}",
                "Content-Type": "application/json",
            },
            json=payload,
        )
        wall = round(time.perf_counter() - t0, 3)
        r.raise_for_status()
        text = r.json()["choices"][0]["message"]["content"].strip()

    else:  # openai_chat
        payload = {
            "model": cfg["model"],
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            "temperature": 0.1,
        }
        t0 = time.perf_counter()
        r = await client.post(
            cfg["url"],
            headers={**cfg["headers"](), "Content-Type": "application/json"},
            json=payload,
        )
        wall = round(time.perf_counter() - t0, 3)
        r.raise_for_status()
        text = r.json()["choices"][0]["message"]["content"].strip()

    return text, wall


# ---------------------------------------------------------------------------
# Agent: Router
# ---------------------------------------------------------------------------
ROUTER_SYSTEM = """You are a router for a DAW AI assistant. Classify the user's request into one or more agents.

COMMAND — Modifying or creating project elements: create/delete/rename tracks, add/move/duplicate/delete clips, add FX (reverb, EQ, compressor), set volume/pan/mute/solo, quantize/transpose notes, set tempo.
Examples: "create a bass track", "delete track 2", "add reverb to vocals", "mute the drums", "quantize to 1/16", "add a 4 bar clip on track 1", "set volume to -6 dB"

MUSIC — Generating musical content: suggest/generate chord progressions, suggest chords, harmonize melodies, generate chord loops.
Examples: "suggest chords in D minor", "give me a jazz ii-V-I", "generate a blues progression", "harmonize this melody"

BOTH — The request requires musical content generation AND project modification. The music agent generates the content, then the command agent executes it.
Examples: "create a piano track with a jazzy chord progression", "add a blues bass line to track 2", "make a new track and write a neo-soul progression on it"

Respond with ONLY: COMMAND, MUSIC, or BOTH."""

ROUTER_CASES = [
    ("create a bass track", "COMMAND"),
    ("delete track 2", "COMMAND"),
    ("add reverb and EQ to the Vocals track", "COMMAND"),
    ("quantize the notes to 1/16", "COMMAND"),
    ("set volume of track 3 to -6 dB", "COMMAND"),
    ("suggest a chord progression in D minor", "MUSIC"),
    ("give me a jazzy ii-V-I in Bb", "MUSIC"),
    ("suggest chords for a blues in G", "MUSIC"),
    ("generate a neo-soul progression", "MUSIC"),
    ("harmonize this melody: E F# G A B", "MUSIC"),
    ("create a piano track with a jazzy chord progression", "BOTH"),
    ("add a blues bass line to track 2", "BOTH"),
    ("make a new track and write a neo-soul progression on it", "BOTH"),
]

# ---------------------------------------------------------------------------
# Agent: Commands (compact DSL)
# ---------------------------------------------------------------------------
COMMAND_SYSTEM = """You are MAGDA, an AI assistant for a DAW.
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
"create a bass track" -> TRACK Bass
"delete track 1" -> DEL 1
"mute all tracks named Drums" -> MUTE Drums
"add reverb and EQ to Vocals" ->
FX Vocals reverb
FX Vocals eq
"set volume of track 3 to -6 dB and pan left" -> SET 3 vol=-6 pan=-1"""

COMMAND_CASES = [
    {"prompt": "create a bass track", "expected": ["TRACK", "Bass"]},
    {"prompt": "delete track 2", "expected": ["DEL", "2"]},
    {"prompt": "mute all tracks named Drums", "expected": ["MUTE", "Drums"]},
    {"prompt": "add a 4 bar clip at bar 1 on track 1", "expected": ["CLIP", "1", "4"]},
    {"prompt": "add reverb and EQ to the Vocals track", "expected": ["FX", "Vocals", "reverb", "eq"]},
    {"prompt": "set volume of track 3 to -6 dB and pan it left", "expected": ["SET", "3", "vol=-6"]},
]

# ---------------------------------------------------------------------------
# Agent: Chord suggestions
# ---------------------------------------------------------------------------
CHORD_SYSTEM = """You are a chord suggestion engine for a DAW. Given context about the current key, recent chords, and/or a mood description, suggest the next 4 chords.

Respond ONLY with CHORD instructions. No prose. One per line.
CHORD <root> <quality> <beat> <length>

Rules:
- Stay in key unless chromatic movement is musically justified
- Consider voice leading and smooth transitions
- Match the requested mood/style

EXAMPLES:
Context: Key: C major. Mood: pop. Recent: C major, G major
CHORD A3 minor 0 4
CHORD F3 major 4 4
CHORD G3 major 8 4
CHORD C3 major 12 4

Context: Key: A minor. Mood: jazzy. Recent: Am7, Dm7
CHORD E3 dom7 0 4
CHORD A3 min7 4 4
CHORD D3 min7 8 4
CHORD G3 dom7 12 4"""

CHORD_CASES = [
    {
        "prompt": "Key: C major. Recent: C major → Am minor → F major → G major. Continue.",
        "label": "Pop I-vi-IV-V continue",
        "check_in_key": ["C", "D", "E", "F", "G", "A", "B"],
    },
    {
        "prompt": "Key: A minor. Recent: Am minor → Dm minor → E dom7. Continue.",
        "label": "Minor i-iv-V7 continue",
        "check_in_key": ["A", "B", "C", "D", "E", "F", "G"],
    },
    {
        "prompt": "Key: Bb major. Mood: jazzy. Recent: Bbmaj7 → Cm7 → Dm7. Continue.",
        "label": "Jazz in Bb continue",
        "check_in_key": ["Bb", "C", "D", "Eb", "F", "G", "A"],
    },
    {
        "prompt": "Mood: soulful, R&B. Key: Eb major. Suggest 4 chords.",
        "label": "Soulful R&B in Eb",
        "check_in_key": ["Eb", "F", "G", "Ab", "Bb", "C", "D"],
    },
    {
        "prompt": "Mood: dark, cinematic. Key: E minor. Suggest 4 chords.",
        "label": "Dark cinematic Em",
        "check_in_key": ["E", "F", "G", "A", "B", "C", "D"],
    },
    {
        "prompt": "Mood: latin, bossa nova. Key: F major. Suggest 4 chords.",
        "label": "Bossa nova in F",
        "check_in_key": ["F", "G", "A", "Bb", "C", "D", "E"],
    },
]

# ---------------------------------------------------------------------------
# Per-provider benchmark (sequential calls within a provider)
# ---------------------------------------------------------------------------
async def run_provider(client: httpx.AsyncClient, provider: str) -> dict:
    """Run all benchmarks for one provider. Returns results dict."""
    print(f"\n{'=' * 80}")
    print(f"  {provider}")
    print(f"{'=' * 80}")

    # Router
    print(f"\n  --- Router ---")
    r_correct, r_total, r_avg = 0, len(ROUTER_CASES), 0.0
    total_time = 0.0
    try:
        for prompt, expected in ROUTER_CASES:
            try:
                result, wall = await call_llm(client, provider, ROUTER_SYSTEM, prompt)
                result = result.strip().upper()
                if result == expected:
                    r_correct += 1
                total_time += wall
            except Exception as e:
                print(f"    ERROR: {e}")
        r_avg = total_time / r_total if r_total else 0
        print(f"  Accuracy: {r_correct}/{r_total} ({round(r_correct/r_total*100)}%)  avg: {r_avg:.2f}s")
    except Exception as e:
        print(f"  FAILED: {e}")

    # Commands
    print(f"\n  --- Commands ---")
    c_correct, c_total, c_avg = 0, len(COMMAND_CASES), 0.0
    total_time = 0.0
    try:
        for tc in COMMAND_CASES:
            try:
                result, wall = await call_llm(client, provider, COMMAND_SYSTEM, tc["prompt"])
                hits = sum(1 for e in tc["expected"] if e.lower() in result.lower())
                if hits == len(tc["expected"]):
                    c_correct += 1
                total_time += wall
            except Exception as e:
                print(f"    ERROR: {e}")
        c_avg = total_time / c_total if c_total else 0
        print(f"  Accuracy: {c_correct}/{c_total} ({round(c_correct/c_total*100)}%)  avg: {c_avg:.2f}s")
    except Exception as e:
        print(f"  FAILED: {e}")

    # Chords
    print(f"\n  --- Chord Suggestions ---")
    chord_results = []
    total_time = 0.0
    ch_avg = 0.0
    try:
        for tc in CHORD_CASES:
            try:
                result, wall = await call_llm(client, provider, CHORD_SYSTEM, tc["prompt"])
                total_time += wall
                lines = [l.strip() for l in result.splitlines() if l.strip().startswith("CHORD")]
                has_format = len(lines) >= 2
                in_key_count = 0
                for line in lines:
                    parts = line.split()
                    if len(parts) >= 5:
                        root = parts[1].rstrip("0123456789")
                        if root in tc["check_in_key"]:
                            in_key_count += 1
                in_key_pct = round(in_key_count / len(lines) * 100) if lines else 0
                chord_results.append({
                    "label": tc["label"], "output": result,
                    "has_format": has_format, "chord_count": len(lines),
                    "in_key_pct": in_key_pct, "wall": wall,
                })
            except Exception as e:
                print(f"    ERROR on {tc['label']}: {e}")
                chord_results.append({"label": tc["label"], "error": str(e)})

        ch_avg = total_time / len(CHORD_CASES) if CHORD_CASES else 0
        format_ok = sum(1 for r in chord_results if r.get("has_format"))
        avg_in_key = round(sum(r.get("in_key_pct", 0) for r in chord_results) / len(chord_results)) if chord_results else 0
        print(f"  Format OK: {format_ok}/{len(chord_results)}  In-key avg: {avg_in_key}%  avg: {ch_avg:.2f}s")
        for r in chord_results:
            if "error" in r:
                print(f"    {r['label']}: ERROR")
            else:
                print(f"    {r['label']}: {r['chord_count']} chords, {r['in_key_pct']}% in key, {r['wall']:.2f}s")
                for line in r["output"].splitlines()[:6]:
                    print(f"      {line}")
    except Exception as e:
        print(f"  FAILED: {e}")

    return {
        "router": {"correct": r_correct, "total": r_total, "avg_s": r_avg},
        "commands": {"correct": c_correct, "total": c_total, "avg_s": c_avg},
        "chords": {"avg_s": ch_avg, "cases": chord_results},
    }


# ---------------------------------------------------------------------------
# Main — run all providers in parallel
# ---------------------------------------------------------------------------
async def main():
    print("=" * 80)
    print("CLOUD MODEL BENCHMARK — Router / Commands / Chord Suggestions")
    print("=" * 80)

    async with httpx.AsyncClient(timeout=120) as client:
        tasks = {p: asyncio.create_task(run_provider(client, p)) for p in PROVIDERS}
        all_results = {}
        for provider, task in tasks.items():
            all_results[provider] = await task

    # Summary table
    print(f"\n{'=' * 80}")
    print("SUMMARY")
    print(f"{'=' * 80}")
    print(f"{'Provider':<20} {'Router':>10} {'Commands':>10} {'Chords fmt':>12} {'In-key':>8} {'Avg latency':>12}")
    print("-" * 80)
    for provider, data in all_results.items():
        r = data["router"]
        c = data["commands"]
        ch = data["chords"]
        fmt_ok = sum(1 for x in ch.get("cases", []) if x.get("has_format"))
        avg_ik = round(sum(x.get("in_key_pct", 0) for x in ch.get("cases", [])) / max(len(ch.get("cases", [])), 1))
        total_avg = (r["avg_s"] + c["avg_s"] + ch["avg_s"]) / 3
        print(f"{provider:<20} {r['correct']}/{r['total']:>5} {c['correct']}/{c['total']:>6} {fmt_ok}/{len(ch.get('cases', [])):>8} {avg_ik:>6}% {total_avg:>10.2f}s")

    # Save results
    RESULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    run = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "providers": all_results,
    }
    with open(RESULTS_FILE, "a") as f:
        f.write(json.dumps(run, default=str) + "\n")
    print(f"\nResults saved to {RESULTS_FILE}")


if __name__ == "__main__":
    asyncio.run(main())
