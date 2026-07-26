"""Generate tokenizer parity cases for the C++ Unigram implementation.

`magda/agents/unigram_tokenizer.cpp` is a hand-written mirror of the Hugging
Face Unigram pipeline. A 128k-entry vocab with float log-probs and a Viterbi
search has far more room to drift than the old word-level regex did, and the
DSL-level parity fixture cannot isolate a tokenizer bug from a model bug — a
one-subword shift usually still produces *some* plausible DSL.

So this dumps (word -> subword ids) directly, over three pools:

  real      every distinct word in the committed eval sets and training data
  shapes    the forms the command surface actually produces (aliases, pitches,
            numbers, glued units, mixed case, punctuation)
  random    adversarial junk, to catch unknown-byte and boundary handling

    python -m model.export_tokenizer_cases --n-random 3000
"""
from __future__ import annotations

import argparse
import json
import os
import random
import string
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from dataset.tagging import tokenize  # noqa: E402
from model.data_encoder import canon  # noqa: E402
from model.net_encoder import load_tokenizer  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)

SHAPES = [
    "<alias>", "<alias.param>", "@serum", "@pro_q_3", "@fm_0", "@1176",
    "C4", "Eb3", "-6", "db", "dB", "0.5", "16ths", "8th", "127", "-12",
    "Krunchy", "Kick", "Reese", "Bass", "bass", "BASS", "bAsS",
    "don't", "arent", "i", "a", "the", "track", "rack", "tracks",
    "hyphen-word", "under_score", "MixedCase", "x", "", " ", "  ",
    "verylongtrackname", "supercalifragilistic", "0", "000", "1e5",
]


def word_pool():
    """Every distinct word the model has actually been fed, post-canon."""
    words = set()
    for rel in ("data/train.jsonl", "data/val.jsonl", "eval/testset.jsonl",
                "eval/ood_testset.jsonl", "eval/dev_testset.jsonl"):
        path = os.path.join(POC, rel)
        if not os.path.exists(path):
            continue
        for line in open(path, encoding="utf-8"):
            if line.strip():
                for w in tokenize(json.loads(line)["input"]):
                    words.add(canon(w))
    return sorted(words)


def random_words(n, seed=11):
    r = random.Random(seed)
    alphabet = string.ascii_letters + string.digits + "_-'.@<>#"
    out = []
    for _ in range(n):
        length = r.randint(1, 18)
        out.append("".join(r.choice(alphabet) for _ in range(length)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--artifacts", default=os.path.join(HERE, "artifacts_onnx"))
    ap.add_argument("--n-random", type=int, default=3000)
    ap.add_argument("--out", default=os.path.join(
        os.path.dirname(POC), "..", "tests", "unigram_tokenizer_cases.json"))
    args = ap.parse_args()

    maps = json.load(open(os.path.join(args.artifacts, "maps.json"), encoding="utf-8"))
    tok = load_tokenizer(maps["model_name"])

    pools = {
        "real": word_pool(),
        "shapes": SHAPES,
        "random": random_words(args.n_random),
    }

    cases = []
    seen = set()
    for pool, words in pools.items():
        for w in words:
            if w in seen:
                continue
            seen.add(w)
            # Encode as a single pre-split word, then drop [CLS]/[SEP]: the C++
            # `pieces()` covers segmentation, `encodeWords()` covers the wrapper.
            ids = tok([w], is_split_into_words=True, add_special_tokens=False)["input_ids"]
            cases.append({"pool": pool, "word": w, "ids": ids,
                          "pieces": tok.convert_ids_to_tokens(ids)})

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"model_name": maps["model_name"], "cases": cases}, f, ensure_ascii=False)

    by_pool = {p: sum(1 for c in cases if c["pool"] == p) for p in pools}
    print(f"wrote {len(cases)} tokenizer cases ({by_pool}) -> {out}")
    print(f"  size: {os.path.getsize(out) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
