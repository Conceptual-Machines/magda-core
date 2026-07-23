"""Analytic parameter counter for IntentSlotNet (mirrors net.py).

Pure Python — no torch/brevitas needed. Reproduces the trained model's
43,782-param count and lets us sweep the size knobs (embed, hidden) to see
where to cut. The +4 vs a naive weight count is the learnable quant scale on
each QuantIdentity/QuantReLU (1 each: inq + b1 + b2 + b3).

Usage:
  python3 model/param_count.py                # default 48/64, from maps.json
  python3 model/param_count.py --embed 32 --hidden 32
  python3 model/param_count.py --sweep
"""
from __future__ import annotations

import argparse
import json
import os

HERE = os.path.dirname(__file__)
MAPS = os.path.join(HERE, "artifacts", "maps.json")

# QuantIdentity (inq) + 3x QuantReLU each carry one learnable activation scale.
QUANT_SCALES = 4


def breakdown(vocab, n_intents, n_tags, embed=48, hidden=64):
    conv = lambda ci, co: co * ci * 3 + co  # kernel_size=3, bias
    lin = lambda ci, co: co * ci + co       # bias
    rows = [
        ("embed      (Embedding vocab x embed)", vocab * embed),
        ("b1 conv    (embed -> hidden, k3 d1) ", conv(embed, hidden)),
        ("b2 conv    (hidden-> hidden, k3 d2) ", conv(hidden, hidden)),
        ("b3 conv    (hidden-> hidden, k3 d4) ", conv(hidden, hidden)),
        ("slot_head  (hidden-> n_tags)        ", lin(hidden, n_tags)),
        ("intent_head(hidden-> n_intents)     ", lin(hidden, n_intents)),
        ("quant scales (inq + 3x QuantReLU)   ", QUANT_SCALES),
    ]
    return rows


def show(vocab, n_intents, n_tags, embed, hidden):
    rows = breakdown(vocab, n_intents, n_tags, embed, hidden)
    total = sum(n for _, n in rows)
    print(f"\nembed={embed}  hidden={hidden}  "
          f"(vocab={vocab}, intents={n_intents}, tags={n_tags})")
    for name, n in rows:
        print(f"  {name}  {n:>7,}  {100*n/total:4.1f}%")
    print(f"  {'TOTAL':<38}  {total:>7,}")
    print(f"  ~size @ 4-bit weights: {total*0.5/1024:.1f} KB")
    return total


def main():
    m = json.load(open(MAPS))
    vocab, n_intents, n_tags = len(m["vocab"]), len(m["intents"]), len(m["tags"])

    ap = argparse.ArgumentParser()
    ap.add_argument("--embed", type=int, default=48)
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--sweep", action="store_true")
    args = ap.parse_args()

    if args.sweep:
        base = show(vocab, n_intents, n_tags, 48, 64)
        for embed, hidden in [(48, 48), (32, 48), (48, 32), (32, 32), (24, 24), (16, 16)]:
            t = show(vocab, n_intents, n_tags, embed, hidden)
            print(f"    -> {100*(base-t)/base:.0f}% smaller than 48/64")
    else:
        show(vocab, n_intents, n_tags, args.embed, args.hidden)


if __name__ == "__main__":
    main()
