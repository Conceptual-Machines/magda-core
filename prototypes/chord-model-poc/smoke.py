"""End-to-end pipeline check — no dataset download required.

Exercises representation -> tokenization -> n-gram training -> eval on a small
set of canonical, uncopyrightable progressions (I-V-vi-IV, ii-V-I, 12-bar
blues, etc.). This proves the Phase-0 machinery runs and round-trips; it is NOT
training data (real training uses the corpora in dataset/sources.py).

Run:  python smoke.py
"""
from __future__ import annotations

from baseline.ngram import NGramModel
from chords.roman import (chord_to_token, token_to_root, roman_display,
                          infer_key, pc_histogram_from_chords)
from chords.vocab import build_vocab
from eval import metrics

# (name, tonic_pc, mode, [(root_pc, quality), ...]) — well-known progressions.
CANON = [
    ("I-V-vi-IV in C", 0, "maj",
     [(0, "maj"), (7, "maj"), (9, "min"), (5, "maj")] * 2),
    ("ii-V-I in C", 0, "maj",
     [(2, "min7"), (7, "dom7"), (0, "maj7")] * 3),
    ("50s I-vi-IV-V in C", 0, "maj",
     [(0, "maj"), (9, "min"), (5, "maj"), (7, "maj")] * 2),
    ("12-bar blues in A", 9, "maj",
     [(9, "dom7"), (9, "dom7"), (9, "dom7"), (9, "dom7"),
      (2, "dom7"), (2, "dom7"), (9, "dom7"), (9, "dom7"),
      (4, "dom7"), (2, "dom7"), (9, "dom7"), (4, "dom7")]),
    ("i-VI-III-VII in A min", 9, "min",
     [(9, "min"), (5, "maj"), (0, "maj"), (7, "maj")] * 2),
    ("vi-IV-I-V in C", 0, "maj",
     [(9, "min"), (5, "maj"), (0, "maj"), (7, "maj")] * 2),
]


def to_tokens(tonic_pc, chords):
    return [chord_to_token(r, q, tonic_pc) for r, q in chords]


def main():
    # 1. Representation round-trips.
    for _, tonic, _, chords in CANON:
        for r, q in chords:
            tok = chord_to_token(r, q, tonic)
            back = token_to_root(tok, tonic)
            assert back == (r, q), f"round-trip failed: {tok} -> {back} != {(r, q)}"
    print("representation round-trip: OK")

    # 2. Key inference sanity on the C-major progressions (from chord tones).
    hist = pc_histogram_from_chords(CANON[0][3])
    print(f"inferred key of '{CANON[0][0]}': {infer_key(hist)}  (expect (0, 'maj'))")

    # 3. Tokenize, show roman rendering of one.
    streams = [to_tokens(t, c) for _, t, _, c in CANON]
    vocab = build_vocab(streams)
    print(f"vocab size: {len(vocab)} tokens")
    print("ii-V-I as roman:", " ".join(roman_display(t) for t in streams[1][:3]))

    # 4. Train the n-gram floor on all but the last, eval on the last.
    train, held = streams[:-1], streams[-1:]
    ng = NGramModel(order=3).train(train + held)  # tiny set: fit then probe
    print(metrics.format_report(metrics.report(ng, held), title="n-gram (order 3) on held progression:"))

    # 5. Continuation demo: given I-V, what's next?
    ctx = [chord_to_token(0, "maj", 0), chord_to_token(7, "maj", 0)]
    preds = ng.topk(ctx, k=3)
    print("after  I  V  ->", ", ".join(f"{roman_display(t)} ({p:.2f})" for t, p in preds))
    print("\nsmoke OK")


if __name__ == "__main__":
    main()
