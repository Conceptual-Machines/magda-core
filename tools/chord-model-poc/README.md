# MAGDA chord-model POC

A tiny, local, learned model of **harmonic idiom** — trained on real chord
progressions — that plugs into MAGDA's existing chord engine. Sibling to
`tools/command-model-poc/` but a different beast: this one is *generative*
(sequential), so the architecture differs. See `findings.md` for the full
design rationale and decisions.

## What it is

One small model of the chord-progression distribution, queried three ways
(all Phase-by-Phase, same weights):

1. **Continuation** — given the last few chords, suggest the next ones.  ← first build
2. **Reharmonization** — given a progression, suggest substitutions per slot.
3. **Generation** — given key + mood, produce a whole progression.

## Why not synthetic data

Rule-generated progressions can only replay the rules you hand-code — they
can't capture *idiom*. So we train on **real** progressions, using only
sources that are safe for a commercial (GPLv3 open-core) product:

| source | license | role |
|---|---|---|
| McGill Billboard | CC0 | primary — real chart-pop harmony |
| Nottingham | public domain (trad. folk) | folk idiom |
| OpenEWLD | public-domain subset | standards / older tunes |

Bare chord progressions are generally not copyrightable (functional building
blocks — the basis iReal Pro operates on), and these annotation sets are
CC0 / public-domain. See `dataset/sources.py` for URLs, citations, and the
manual download steps (we do NOT auto-scrape anything).

## Representation (the key idea)

Chords are modelled **key-relative**: a token is `(interval-from-tonic,
quality-class)`, e.g. `7:dom7` = "V7". Key-relative ⇒ transposition-invariant
⇒ tiny vocab (~50–150 tokens) ⇒ tiny model, and it generalizes across keys.
The model never emits MIDI — a deterministic step maps `(interval, quality)`
back to a concrete chord in the current key (mirrors the command model's
"classify + deterministic reconstruct" guarantee). See `chords/`.

## Layout

```
chords/     representation: key-relative tokens, key inference   [works now]
baseline/   KN-smoothed n-gram next-chord model (Phase 0 floor)  [works now]
eval/       top-k accuracy, perplexity, in-key %, cadence rate   [works now]
dataset/    source registry + parsers + token-stream builder     [needs data]
model/      tiny GRU next-token model + training loop (Phase 1)   [needs data]
smoke.py    end-to-end pipeline check on canonical progressions   [works now]
```

## Quick start (no dataset needed)

```bash
python3 -m venv .venv && ./.venv/bin/pip install -r requirements.txt
./.venv/bin/python smoke.py          # proves representation + n-gram + eval
```

Then, to train on real data: follow `dataset/sources.py` to download the
corpora into `data/raw/`, run `python -m dataset.build`, and go from there.
