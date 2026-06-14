# MAGDA Tiny Command Model - POC

Natural language -> validated **MAGDA DSL** for safe DAW automation
(create/rename/delete tracks, add plugins, group, mute/solo, colour). Not music
generation, not mixing, not parameter tweaking - structural commands only.

## What the final output is

One shippable artifact: a **tiny quantized model (GGUF)** that runs locally on
the `third_party/llama.cpp` already in this repo (via `LlamaLocalClient`) and
converts a request into the DSL the **existing** `CommandAgent` pipeline already
executes:

```
"create a bass track with serum and ott"
   -> tiny local model (<500ms, offline, free)
track(name="Bass", new=true).fx.add(name="<serum>")
track(name="Bass").fx.add(name="<ott>")
   -> CompactParser/dsl_interpreter -> InstructionExecutor -> UndoManager (one undoable transaction)
```

The apply / preview / undo half already exists in `magda/agents` + `magda/daw`.
This POC only produces the model and the data/eval rig that makes it. The model
emits the **shipping DSL** (`magda/agents/dsl_grammar.hpp`), so anything it
produces is already executable.

## Why emit the existing DSL (not new JSON)

The output grammar is frozen, CFG-constrained in production, and wired to apply
+ undo. The model's only job is `text -> DSL`; the renderer + grammar guarantee
correctness on the output side.

## Multilingual: one model, not per-language

Locale set mirrors MAGDA's Crowdin translations: **en, ja, ru, zh**.

The DSL **output is language-invariant** - `track(name="Bass")...` is identical
regardless of input language; only the request varies. A tiny model spends most
of its capacity learning to emit valid DSL, and that half is shared across
languages. Splitting by language re-learns the same grammar N times and adds a
runtime language router. So: **one multilingual model**, report **per-language
accuracy**, and only consider a split (or an EN-core + language packs) if the
eval proves a single tiny model can't hold all locales. Decision is data-driven.

Design call: localized requests map to **canonical English track names**
(`ベース`/`贝斯`/`бас` -> `name="Bass"`); plugin tokens and colour hexes are
already language-neutral.

## Layout

```
magda_dsl/
  grammar.lark     # mirrored verbatim from dsl_grammar.hpp (the CFG)
  vocab.py         # plugin inventory, colour palette, supported cmds + gaps
  dsl.py           # canonical action -> DSL renderer (gold by construction)
  i18n.py          # ja/ru/zh curated seed banks (output = canonical English plan)
  validate.py      # lark parse + semantic checks
dataset/generate.py    # template-based synthetic data (en procedural + curated)
baseline/rule_parser.py# Phase 1 no-model NL->DSL baseline (English)
eval/make_testset.py   # build fixed testset.jsonl (hand-authored intents)
eval/metrics.py        # valid-syntax / valid-command / exact-match / latency
eval/run.py            # run a parser/model vs the test set
```

## Run

```bash
pip install -r requirements.txt          # lark
python3 -m eval.make_testset             # writes eval/testset.jsonl (committed)
python3 -m dataset.generate --n 500      # writes data/train.jsonl
python3 -m eval.run                      # baseline metrics, per language
python3 -m eval.run --show-fails         # dump mismatches
```

## Metrics & targets (POC spec, adapted JSON->DSL)

`valid_syntax` (parses under grammar.lark) >= 95%, `valid_command` >= 90%,
`exact_match` >= 80%, latency < 500ms. The harness reports each overall and
per language.

Current baseline (no model): **en = 100% / 100% / 100%**, **ja/ru/zh = 0%**
(the regex baseline is English-only; the 0% is the gap the trained model closes).

## Supported commands vs grammar gaps

Expressible in the shipping DSL today (modelled in v0): `create_track`,
`add_plugin`, `rename_track`, `delete_track`, `mute_track`, `solo_track`,
`set_track_volume`, `set_track_pan`, `set_track_color`, `group_tracks`,
`select_all_clips`, `select_clips_named`, `select_clips_type`,
`select_clips_longer_than`, `select_clips_shorter_than`,
`select_clips_starting_after`, `select_clips_starting_before`,
`select_all_clips_rename`.

In the spec but **NOT** in the current DSL grammar (excluded from v0; each needs
a grammar + interpreter addition before it can be modelled) - see
`vocab.GRAMMAR_GAPS`:

- `duplicate_track` - no `track.duplicate()` (TE `DuplicateTrackCommand` exists, unexposed)
- `remove_plugin` - no `fx.remove()`
- `route_track` - no send/route method (`TrackManager::addSend` exists, unexposed)
- substring grouping ("tracks containing bass") - filter is exact `==` only

## Data generation: no LLM teacher required

- **DSL side (output):** always deterministic - templates/renderer or the
  existing CFG-constrained `CommandAgent`. Never an LLM (it would only add
  errors to validate away).
- **NL side (input):** templates for v0/v1. An LLM teacher is an **optional v2
  lever** for phrasing variety + multilingual breadth, and is **pluggable**
  (Claude via the existing `createLLMClient`, or a local open-weight for a $0
  offline 50k run). Not pinned to any model.

## Training (off-box) -> eval (local)

The full loop. Training runs on Colab; everything else is local.

```bash
# 1. freeze data (local) -- train + held-out val, leakage-guarded vs test
python3 -m dataset.generate --n 12000 --val 1500

# 2. lock the prompt contract (local) -- chat records w/ the system prompt
#    that BOTH training and C++ inference use (model/format.py: SYSTEM_PROMPT)
python3 -m model.format            # -> data/{train,val}.chat.jsonl

# 3. train (Colab) -- open model/train_colab.ipynb in a GPU runtime.
#    It exports /content/drive/MyDrive/magda-command-model/command-model.gguf.

# 4. copy the exported GGUF into the app-visible artifact path
mkdir -p model/artifacts
cp "/path/to/Google Drive/My Drive/magda-command-model/command-model.gguf" model/artifacts/command-model.gguf

# 5. score the model on the SAME fixed test set (local M1, real latency)
pip install llama-cpp-python
python3 -m eval.run --model model/artifacts/command-model.gguf
python3 -m eval.run --model model/artifacts/command-model.gguf --show-fails
```

`SYSTEM_PROMPT` in `model/format.py` is the inference contract: the C++ command
backend must send the identical string. It is deliberately short (fine-tuned
weights carry the mapping) for latency. Final artifact:
`model/artifacts/command-model.gguf`. The app's `FAST_INFERENCE` command backend
loads that GGUF through `LlamaModelManager` / llama.cpp. No standing GPU infra;
the M1 is the latency test bed.

## Roadmap

- v0 500 / v1 5k / v2 50k examples
- Phase 1 rule baseline (done) -> Phase 2 small fine-tuned student -> Phase 3
  optional teacher-paraphrased inputs
- Wire the chosen GGUF into `LlamaLocalClient` as the offline command backend
