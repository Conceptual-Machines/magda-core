"""Synthetic dataset for the router: request text -> ConsoleIntent label.

Templates x slot pools, per language, per label. Labels are correct by
construction (the template *is* the label), so there is no teacher model and no
validation pass — the router has no structured output to get wrong, only a
category.

The English COMMAND class additionally samples the command model's own training
corpus (../command-model-poc/data/train.jsonl). That is deliberate: COMMAND
means "the on-device command model can execute this", so the cleanest possible
definition of the class is the exact input distribution that model was trained
on. If the two ever drift apart the router starts handing the command model
requests it cannot parse.

    python -m router.generate --n 800 --val 0.1
"""
from __future__ import annotations

import argparse
import json
import os
import random
import re
import string
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from router import seeds_en, seeds_ja, seeds_ru, seeds_zh  # noqa: E402
from router.data import open_rows  # noqa: E402
from router.labels import LABELS  # noqa: E402
from router.synonyms import BY_LANG  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)
COMMAND_CORPUS = os.path.join(POC, os.pardir, "command-model-poc", "data", "train.jsonl")

LANG_SEEDS = {"en": seeds_en, "ja": seeds_ja, "ru": seeds_ru, "zh": seeds_zh}

# Real messages carry filler. Only English gets a politeness bank rich enough to
# be worth sampling; ja/ru/zh forms are attached to specific verb conjugations,
# so a blanket prefix would produce ungrammatical seeds.
POLITENESS = {
    "en": (["", "", "", "please ", "can you ", "could you ", "hey ",
            "i want you to ", "pls ", "let's "],
           ["", "", "", "", " please", " for me", " thanks"]),
    "ja": (["", "", "", "ちょっと"], [""]),
    "ru": (["", "", "", "пожалуйста, ", "давай "], ["", "", "", ", пожалуйста"]),
    "zh": (["", "", "", "帮我", "请"], [""]),
}

_FORMATTER = string.Formatter()


def slots_of(template: str):
    return [f for _, f, _, _ in _FORMATTER.parse(template) if f]


def synonymize(template: str, syn: dict, rng: random.Random, p: float) -> str:
    """Reword the template's literal text, leaving {placeholders} untouched."""
    parts = re.split(r"(\{[^}]*\})", template)
    for i, seg in enumerate(parts):
        if seg.startswith("{"):
            continue
        for key, alts in syn.items():
            if key not in seg or rng.random() > p:
                continue
            alt = rng.choice(alts)
            if key.isascii():
                seg = re.sub(rf"\b{re.escape(key)}\b", alt.replace("\\", r"\\"),
                             seg, count=1)
            else:
                seg = seg.replace(key, alt, 1)
        parts[i] = seg
    return "".join(parts)


def expand(template: str, pools: dict, rng: random.Random) -> str:
    return template.format(**{s: rng.choice(pools[s]) for s in slots_of(template)})


def decorate(text: str, lang: str, rng: random.Random) -> str:
    pre, suf = POLITENESS[lang]
    return rng.choice(pre) + text + rng.choice(suf)


def capacity(template: str, pools: dict, lang: str, syn: dict) -> int:
    """How many distinct strings this template can produce, filler included.

    MIXING and SESSION are questions, not parameterized commands, so most of
    their phrasings carry no slots at all — counting only slot combinations
    would cap those classes at one row per template and starve them.
    """
    n = 1
    for s in slots_of(template):
        n *= len(pools[s])
    literal = re.sub(r"\{[^}]*\}", " ", template)
    for key, alts in syn.items():
        if key in literal:
            n *= len(alts)
    pre, suf = POLITENESS[lang]
    return n * len(set(pre)) * len(set(suf))


def load_command_corpus(limit: int, rng: random.Random):
    """English COMMAND rows straight from the command model's training data."""
    path = os.path.abspath(COMMAND_CORPUS)
    if not os.path.exists(path):
        print(f"  ! command corpus not found ({path}), templates only")
        return []
    inputs = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("lang") == "en":
                inputs.append(row["input"])
    rng.shuffle(inputs)
    return inputs[:limit]


def generate(n_per_class: int, seed: int, excluded: set, syn_p: float):
    rng = random.Random(seed)
    rows, seen = [], set(excluded)

    def emit(text, lang, label):
        if text in seen:
            return False
        seen.add(text)
        rows.append({"lang": lang, "input": text, "label": label})
        return True

    for lang, mod in LANG_SEEDS.items():
        syn = BY_LANG[lang]
        for label in LABELS:
            templates = mod.TEMPLATES[label]
            # Cap per template so one high-arity phrasing can't dominate a class,
            # but leave enough slack (3x the even share) that a class made of
            # low-arity templates can still reach its quota.
            share = 3 * n_per_class // len(templates) + 4
            budget = {t: min(capacity(t, mod.POOLS, lang, syn), share)
                      for t in templates}
            made, tries = 0, 0
            while made < n_per_class and tries < n_per_class * 60:
                if all(b <= 0 for b in budget.values()):
                    break  # every phrasing exhausted — this class is simply smaller
                tries += 1
                t = rng.choice(templates)
                if budget[t] <= 0:
                    continue
                worded = synonymize(t, syn, rng, syn_p)
                text = decorate(expand(worded, mod.POOLS, rng), lang, rng)
                if emit(text, lang, label):
                    budget[t] -= 1
                    made += 1
            print(f"  {lang}/{label:<10} {made:5d} rows from {len(templates)} templates")

    corpus = load_command_corpus(n_per_class * 2, rng)
    added = sum(emit(text, "en", "COMMAND") for text in corpus)
    print(f"  en/COMMAND +{added} rows sampled from the command-model corpus")
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=800, help="rows per (language, label)")
    ap.add_argument("--val", type=float, default=0.1, help="held-out fraction")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--syn-p", type=float, default=0.6,
                    help="per-key probability of swapping in a synonym")
    ap.add_argument("--out", default=os.path.join(POC, "data"))
    args = ap.parse_args()

    test_path = os.path.join(POC, "eval", "testset.jsonl")
    excluded = set()
    if os.path.exists(test_path):
        with open(test_path, encoding="utf-8") as f:
            excluded = {json.loads(l)["input"] for l in f if l.strip()}
        print(f"leakage guard: {len(excluded)} held-out test inputs excluded")

    rows = generate(args.n, args.seed, excluded, args.syn_p)
    random.Random(args.seed).shuffle(rows)
    cut = int(len(rows) * (1 - args.val))
    os.makedirs(args.out, exist_ok=True)
    for name, part in (("train", rows[:cut]), ("val", rows[cut:])):
        path = os.path.join(args.out, f"{name}.jsonl.gz")
        with open_rows(path, "wt") as f:
            for r in part:
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
        mb = os.path.getsize(path) / 1048576
        print(f"{name}: {len(part)} rows -> {path} ({mb:.2f} MB)")


if __name__ == "__main__":
    main()
