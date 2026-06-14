"""Lock the prompt contract: dataset jsonl -> chat-format SFT records.

The SAME system prompt is used for (a) fine-tuning here and (b) inference in
C++ via LlamaLocalClient. Keep them identical or the shipped model misbehaves.

A fine-tuned model learns the mapping in its weights, so the system prompt is
deliberately SHORT (unlike the full zero-shot getToolDescription() the cloud
CommandAgent sends). Short prompt = lower latency + more room for runtime state.

Emits OpenAI-style {"messages":[system,user,assistant]} jsonl, which unsloth /
TRL SFT and llama.cpp chat templating both consume directly.

    python -m model.format            # data/train.jsonl -> data/train.chat.jsonl (+ val)
"""
from __future__ import annotations

import argparse
import json
import os

# The inference contract. MUST match the string the C++ command backend sends.
SYSTEM_PROMPT = (
    "You convert a music-production request into MAGDA DSL. "
    "Output only DSL, one statement per line, no prose.\n"
    "Grammar: track(name=\"X\", new=true) creates a track; "
    ".fx.add(name=\"eq\") adds an internal FX, .fx.add(name=\"<serum>\") a third-party plugin by alias token; "
    ".track.set(name=, colour=\"#rrggbb\", mute=true, solo=true) edits a track; "
    ".track.group(name=\"G\", tracks=\"1,2,3\") groups tracks by id; "
    ".delete() deletes; filter(tracks, track.name == \"X\").<method> applies in bulk. "
    "Reference an existing track with track(id=N) or track(name=\"X\")."
)


def to_chat(row: dict) -> dict:
    return {
        "lang": row.get("lang", "en"),
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": row["input"]},
            {"role": "assistant", "content": row["output"]},
        ],
    }


def convert(src: str, dst: str) -> int:
    n = 0
    with open(src, encoding="utf-8") as fin, open(dst, "w", encoding="utf-8") as fout:
        for line in fin:
            line = line.strip()
            if not line:
                continue
            fout.write(json.dumps(to_chat(json.loads(line)), ensure_ascii=False) + "\n")
            n += 1
    return n


def main():
    here = os.path.dirname(__file__)
    data = os.path.join(here, "..", "data")
    ap = argparse.ArgumentParser()
    ap.add_argument("--splits", default="train,val", help="comma list of <split>.jsonl to convert")
    args = ap.parse_args()
    for split in [s.strip() for s in args.splits.split(",") if s.strip()]:
        src = os.path.join(data, f"{split}.jsonl")
        if not os.path.exists(src):
            print(f"skip {split}: {src} not found")
            continue
        dst = os.path.join(data, f"{split}.chat.jsonl")
        n = convert(src, dst)
        print(f"{split}: {n} records -> {os.path.abspath(dst)}")


if __name__ == "__main__":
    main()
