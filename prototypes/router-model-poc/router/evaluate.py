"""Score the float reference on the fixed held-out test set.

**Core accuracy is the metric.** Fast inference is deterministic classification
over a fixed label set, so the cases it owns are the ones whose label is
recoverable from a named operation or object. Cases that need an aesthetic
judgement ("too stiff", "feel like sunday morning") are the LLM path's job by
construction and are reported separately — scoring them against this model
measures the wrong thing. See eval/make_testset.py for the split.

The confusion matrix is the other thing to read: the issue accepts fuzzy
one-offs misrouting, but a systematic MUSIC->COMMAND leak would mean music
requests get mangled into DSL, which is not acceptable. Read the off-diagonal,
not the headline number.

    python -m router.evaluate [--show-fails]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from router.labels import LABELS  # noqa: E402
from router.reference import load_reference  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--testset", default=os.path.join(POC, "eval", "testset.jsonl"))
    ap.add_argument("--artifacts", default=os.path.join(POC, "artifacts"))
    ap.add_argument("--show-fails", action="store_true")
    args = ap.parse_args()

    _, _, _, predict = load_reference(args.artifacts)
    with open(args.testset, encoding="utf-8") as f:
        rows = [json.loads(l) for l in f if l.strip()]

    by_lang = defaultdict(lambda: [0, 0])
    by_kind = defaultdict(lambda: [0, 0])
    confusion = Counter()
    fails = []
    t0 = time.perf_counter()
    for r in rows:
        got = predict(r["input"])
        hit = got == r["label"]
        kind = r.get("kind", "core")
        by_kind[kind][0] += hit
        by_kind[kind][1] += 1
        if kind == "core":  # per-language breakdown tracks the metric, not the noise
            by_lang[r["lang"]][0] += hit
            by_lang[r["lang"]][1] += 1
        confusion[(r["label"], got)] += 1
        if not hit:
            fails.append((kind, r["lang"], r["input"], r["label"], got))
    ms = (time.perf_counter() - t0) * 1000 / max(len(rows), 1)

    hit, tot = by_kind["core"]
    print(f"CORE accuracy: {hit}/{tot} = {hit/max(tot,1):.1%}   "
          f"({ms:.2f} ms/request, torch)")
    for lang in sorted(by_lang):
        h, t = by_lang[lang]
        print(f"  {lang}: {h:3d}/{t:3d} = {h/t:.1%}")
    if by_kind["fuzzy"][1]:
        h, t = by_kind["fuzzy"]
        print(f"\nfuzzy (LLM's job by design, not scored): {h}/{t} = {h/t:.0%}")

    width = max(len(x) for x in LABELS)
    print("\nconfusion (rows = gold, cols = predicted)")
    print(" " * (width + 2) + " ".join(f"{x[:4]:>4}" for x in LABELS))
    for gold in LABELS:
        cells = " ".join(f"{confusion[(gold, p)] or '.':>4}" for p in LABELS)
        print(f"  {gold:<{width}} {cells}")

    if args.show_fails and fails:
        print("\nmisroutes:")
        for kind, lang, text, gold, got in sorted(fails):
            tag = f"{kind}/{lang}"
            print(f"  [{tag}] {text!r}\n      gold={gold} got={got}")


if __name__ == "__main__":
    main()
