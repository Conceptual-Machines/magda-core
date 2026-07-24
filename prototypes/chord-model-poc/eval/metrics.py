"""Held-out evaluation for a next-chord model.

A "model" here is any object exposing:
  next_dist(context) -> {token: prob}     (for perplexity)
  topk(context, k)   -> [(token, prob)]   (for accuracy)

Metrics:
  top1 / topk accuracy  — did the true next chord appear in the prediction?
  perplexity            — how surprised the model is by held-out sequences
  in_key_rate           — fraction of predicted chords diatonic to the key
  cadence_rate          — fraction of sequences the model would resolve to I
"""
from __future__ import annotations

# Diatonic intervals of the major/natural-minor scales (relative to tonic).
_DIATONIC = {"maj": {0, 2, 4, 5, 7, 9, 11}, "min": {0, 2, 3, 5, 7, 8, 10}}


def _interval(token):
    return int(token.split(":", 1)[0]) if ":" in token else None


def next_chord_accuracy(model, sequences, k=3):
    top1 = topk = total = 0
    for seq in sequences:
        for i in range(1, len(seq)):
            ctx, true = seq[:i], seq[i]
            preds = [t for t, _ in model.topk(ctx, k)]
            if preds:
                total += 1
                if preds[0] == true:
                    top1 += 1
                if true in preds:
                    topk += 1
    if not total:
        return {"top1": 0.0, f"top{k}": 0.0, "n": 0}
    return {"top1": top1 / total, f"top{k}": topk / total, "n": total}


def in_key_rate(model, sequences, mode="maj", k=1):
    ok = total = 0
    diat = _DIATONIC[mode]
    for seq in sequences:
        for i in range(1, len(seq)):
            preds = model.topk(seq[:i], k)
            if not preds:
                continue
            total += 1
            ivl = _interval(preds[0][0])
            if ivl is not None and ivl in diat:
                ok += 1
    return ok / total if total else 0.0


def cadence_rate(model, sequences):
    """How often the model's top pick after a dominant (V / V7) is the tonic —
    a coarse proxy for 'does it resolve like real music'."""
    resolved = opportunities = 0
    for seq in sequences:
        for i in range(1, len(seq)):
            if _interval(seq[i - 1]) == 7:  # previous chord is on the 5th
                opportunities += 1
                preds = model.topk(seq[:i], 1)
                if preds and _interval(preds[0][0]) == 0:
                    resolved += 1
    return resolved / opportunities if opportunities else 0.0


def report(model, sequences, mode="maj", k=3):
    acc = next_chord_accuracy(model, sequences, k)
    return {
        **acc,
        "perplexity": model.perplexity(sequences),
        "in_key": in_key_rate(model, sequences, mode),
        "cadence": cadence_rate(model, sequences),
    }


def format_report(rep: dict, title: str = "") -> str:
    head = f"{title}\n" if title else ""
    return (head +
            f"  top1={rep.get('top1', 0):.1%}  top{rep.get('n') and 3}="
            f"{rep.get('top3', 0):.1%}  ppl={rep.get('perplexity', 0):.2f}  "
            f"in_key={rep.get('in_key', 0):.1%}  cadence={rep.get('cadence', 0):.1%}  "
            f"n={rep.get('n', 0)}")
