# MAGDA Router Model - POC

On-device **ConsoleIntent classifier**: which agent handles this console turn.
Closes the last network hop in the command path (#1843) — with the command
model (#1827) behind `role::COMMAND` and this behind `role::ROUTER`, a
structural request is answered entirely offline.

```
"add a jazzy progression to the keys track"
   -> router model (<1ms, offline, free)      -> BOTH
   -> music agent + command agent
```

## Why a separate model, not another head on the command model

The command model classifies 38 **DSL command** intents and tags slots. The
router answers a coarser, different question — **which agent**. Folding them
together would put a single softmax over two incompatible label spaces, and the
failure it invites is the bad one: a music request scored against command
intents gets mangled into DSL. Keeping the label spaces disjoint is the whole
safety property.

Architecture is the command model's `IntentSlotNet` **minus the slot head**:

```
embedding -> 3 dilated conv1d + ReLU (dilation 1/2/4) -> masked mean-pool -> 7-way head
```

No slot head means no per-token projection and a 7-way instead of 38-way
output, so the net is a fraction of the size. Brevitas QAT is kept so the
FINN/FPGA path stays open; the CPU backend runs the float master weights.

## No fallback, by design

The router classifies and commits — no confidence threshold, no LLM second
opinion. Fast inference is *deterministic classification over a fixed label
set*, not generation. A genuinely open-ended request ("make it punchier") is the
LLM's job by construction, not a router failure to hedge against. Adding a
confidence gate would buy a little accuracy for a lot of complexity and a
non-deterministic path.

## Multilingual: a different tokenizer from the command model

The command model's tokenizer is ASCII-only (`[@#]?[A-Za-z0-9_'-]+`). On
Japanese, Russian or Chinese input it produces **zero tokens** — its shipped
vocab has no non-ASCII entries, so it is English-only in practice.

The router cannot inherit that. It runs on *every* console turn, so an
ASCII-only tokenizer would route all non-Latin traffic to the view default. So
`router/text.py` defines its own rule, small enough to mirror exactly in C++
with no ICU dependency:

- CJK ideographs and kana: **one token per codepoint** (those scripts have no
  spaces, and a dictionary segmenter is not portable to the C++ side).
- Everything else: maximal runs of word codepoints — ASCII alphanumerics plus
  `_ ' -`, and any non-ASCII codepoint outside the CJK and punctuation/symbol
  blocks. Cyrillic, Greek, Hangul and accented Latin therefore behave as normal
  words.
- Case folding covers ASCII, Latin-1 supplement and Cyrillic — every cased
  script in MAGDA's locale set.
- `@alias` plugin references collapse to one opaque `<alias>` token, so the
  vocab never grows with the plugin registry.

`MAX_LEN` is 32 (vs the command model's 24) because character-level CJK needs
the room.

## Layout

```
router/labels.py      # the 7 ConsoleIntent labels, order mirrored by C++
router/text.py        # tokenizer + case folding — the C++ parity contract
router/seeds_{en,ja,ru,zh}.py   # per-language template banks (POOLS + TEMPLATES)
router/synonyms.py    # per-language synonym banks for phrasing diversity
router/generate.py    # templates x pools x synonyms -> data/{train,val}.jsonl.gz
router/data.py        # vocab/maps + dataset (word dropout)
router/net.py         # IntentNet (Brevitas QAT)
router/train.py       # class-weighted CE, select on held-out accuracy
router/reference.py   # plain-float forward — the C++ parity reference
router/evaluate.py    # accuracy per language + confusion matrix
router/export_cpp.py  # -> magda/agents/router_model_data.*, tests fixture
eval/make_testset.py  # hand-authored held-out set (committed)
```

## Run

```bash
python3 -m venv .venv && ./.venv/bin/pip install -r requirements.txt
# (or reuse ../command-model-poc/.venv — same torch + brevitas)

./.venv/bin/python -m eval.make_testset      # eval/testset.jsonl (committed)
./.venv/bin/python -m router.generate --n 2000
./.venv/bin/python -m router.train --epochs 25
./.venv/bin/python -m router.evaluate --show-fails
./.venv/bin/python -m router.export_cpp      # C++ weights + parity fixture
```

Then `make test` — `tests/test_router_model.cpp` locks the C++ backend to the
Python float reference.

## Committed data

`data/*.jsonl.gz` (1.1 MB) and `artifacts/` (344 KB) are committed. The splits
are gzipped because templated text compresses ~6.5x — the same content as plain
JSONL is 7.8 MB.

They are also fully reproducible: `--n 3000 --seed 7` regenerates them
byte-for-byte (given the same command-model corpus). They are kept anyway so the
shipped weights are auditable — you can see exactly what the model was trained
on without re-running anything. `artifacts/model.pt` is *not* optional: the C++
export reads it, and retraining is not bit-reproducible across machines.

## The COMMAND class is the command model's own corpus

`generate.py` samples the English COMMAND class from
`../command-model-poc/data/train.jsonl` rather than only from its own
templates. COMMAND means "the on-device command model can execute this", so the
cleanest definition of the class is the exact input distribution that model was
trained on. If the two drift apart, the router starts handing the command model
requests it cannot parse.

## Metrics

Accuracy on the committed hand-authored test set, overall and per language,
plus a confusion matrix. Read the off-diagonal, not the headline: the issue
accepts fuzzy one-offs misrouting, but a systematic MUSIC -> COMMAND leak means
music requests get mangled into DSL, which is not acceptable.

Cases are tagged **core** (label follows from a named operation or object — the
fast inference surface) or **fuzzy** (needs an aesthetic judgement, so it is the
LLM path's job by design). Core accuracy is the metric; fuzzy is reported
separately and never mixed in.

Current: **81.5% core** (en 82.1 / ja 78.6 / ru 78.6 / zh 85.7), **zero**
MUSIC -> COMMAND confusions, 64k params / ~251 KB of float32. Latency is 0.25 ms
per request in torch and ~1.3 ms in the *debug* C++ build (unoptimised, measured
via the test binary; release is not benchmarked yet). The val split reads 100%
and is meaningless on its own — it is drawn from the same templates. See
findings.md for how the number moved from 59.3% and what is left.
