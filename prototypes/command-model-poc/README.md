# MAGDA Tiny Command Model - POC

Natural language -> validated **MAGDA DSL** for safe DAW automation
(create/rename/delete tracks, add plugins, group, mute/solo, colour). Not music
generation, not mixing, not parameter tweaking - structural commands only.

## What the final output is

A local model that turns a request into the DSL the **existing** `CommandAgent`
pipeline already executes:

```
"create a bass track with serum and ott"
   -> local model (offline, free)
track(name="Bass", new=true).fx.add(name="<serum>")
track(name="Bass").fx.add(name="<ott>")
   -> CompactParser/dsl_interpreter -> InstructionExecutor -> UndoManager (one undoable transaction)
```

The apply / preview / undo half already exists in `magda/agents` + `magda/daw`.
This POC only produces the model and the data/eval rig that makes it. The model
emits the **shipping DSL** (`magda/agents/dsl_grammar.hpp`), so anything it
produces is already executable.

**The model never writes DSL.** It does perception only — classify the intent,
tag each word with its slot role — and a deterministic reconstructor + renderer
build the DSL from those labels. Malformed DSL is therefore structurally
impossible, which is the property a generative model would give up.

Three model shapes exist in this folder, in the order they happened:

| | what it is | status |
|---|---|---|
| `model/net.py` | 51k-param conv net, trained from scratch, no language prior | shipped via #1827; **46.5%** on held-out phrasing |
| `model/net_encoder.py` | pretrained transformer encoder + the same two heads | #1847 recommendation; **91.5%** on the same set (DeBERTa-v3 base) |
| `model/train_colab.py` | generative Qwen2.5-0.5B -> GGUF | parked; kept as a distillation teacher / accuracy ceiling, not the artifact |

## Why emit the existing DSL (not new JSON)

The output grammar is frozen, CFG-constrained in production, and wired to apply
+ undo. The model's only job is `text -> DSL`; the renderer + grammar guarantee
correctness on the output side.

## English only

The POC is English-only, deliberately, as of 2026-07-25. The multilingual
plumbing (`i18n.py` seed banks, `--langs`, `INCLUDE_NON_EN`) has been removed
rather than parked, and the 45 non-English training rows deleted.

They were never doing anything. `dataset/tagging.py:_TOK` is ASCII-only, so
Cyrillic and CJK tokenized to an **empty** sequence — those rows trained the
model on nothing, and both the conv net and a multilingual encoder scored ~13%
on them. See findings.md ("Multilingual is NOT free") for the measurements.

The blocker was never the model. Word segmentation sits in front of it, because
BIO tags are per-word and `reconstruct` consumes word-level tags, and that
segmenter is English-shaped. Whoever picks up #1846 should start there:

- **Latin-script + Cyrillic** (es, fr, de, it, pt, nl, ru): one Unicode-aware
  regex fixes tokenization — verified. Costs a C++ mirror update
  (`command_model.cpp` is byte-based) and a retrain, since the vocab changes.
  Today accented words fragment: `añade` -> `a`, `ade`; `crée` -> `cr`, `e`.
- **Japanese / Chinese**: no whitespace, so "one tag per word" has no natural
  unit. Needs character-level segmentation, or moving BIO tagging to subwords
  and rewriting the span logic in `reconstruct`. A design change.

The DSL output is language-invariant either way — `track(name="Bass")` is the
same in every locale — so the work is all on the input side.

## Layout

```
magda_dsl/
  grammar.lark     # mirrored verbatim from dsl_grammar.hpp (the CFG)
  vocab.py         # plugin inventory, colour palette, supported cmds + gaps
  dsl.py           # canonical action -> DSL renderer (gold by construction)
  validate.py      # lark parse + semantic checks
dataset/generate.py    # template-based synthetic data (English, procedural)
baseline/rule_parser.py# Phase 1 no-model NL->DSL baseline (English)
eval/make_testset.py   # build fixed testset.jsonl (hand-authored intents)
eval/make_dev_testset.py # build dev_testset.jsonl  (held-out; select on this)
eval/make_ood_testset.py # build ood_testset.jsonl  (held-out; sealed, report)
eval/metrics.py        # valid-syntax / valid-command / exact-match / latency
eval/run.py            # run a parser/model vs the test set
model/net.py           # from-scratch conv net (Brevitas QAT, the FPGA shape)
model/net_encoder.py   # pretrained-encoder swap (#1847) - same two heads
model/data_encoder.py  # subword <-> word BIO alignment for the encoder
model/train_encoder.py # fine-tune an encoder; reports OOD score each epoch
```

## Measuring generalisation (read before trusting any number)

There are **three** evaluation sets and confusing them is how the POC ended up
believing a 46.5% model scored 98.2%:

| set | authored from | use it for |
|---|---|---|
| `data/val.jsonl` | the same templates as train | nothing — it saturates at 100% and ranks checkpoints by nothing |
| `eval/testset.jsonl` | the same case list as the templates | template recall only |
| `eval/dev_testset.jsonl` | intent list + DSL semantics, by hand (46 cases) | **tuning and checkpoint selection** |
| `eval/ood_testset.jsonl` | intent list + DSL semantics, by hand (71 cases) | **the reported number — keep it sealed** |

The gap is not small: the shipped conv net scores 98.2% on `testset.jsonl` and
**46.5%** on `ood_testset.jsonl`. Quote the second one.

Both hand-authored sets assert at build time that no case appears verbatim in
train/val or in each other, and the OOD builder reports token-level Jaccard to
the nearest training row, so "held out" is checked rather than claimed.

```bash
python3 -m eval.make_dev_testset                       # 46 cases, tune vs these
python3 -m eval.make_ood_testset                       # 71 cases, report vs these
python3 -m eval.run --torch-model --ood --by-intent    # conv net, held out
python3 -m eval.run --encoder-model model/artifacts_encoder_xlmr --ood --by-intent
```

**Never tune against `ood_testset.jsonl`.** A set you select against stops
predicting how the model handles the next sentence and starts describing the
71 it already saw — the same self-deception as scoring 111/111 on the
templates, one level up. `train_encoder.py` selects on `dev` for exactly this
reason and only prints `ood`.

## Run

```bash
pip install -r requirements.txt          # lark, torch, brevitas, transformers
python3 -m eval.make_testset             # writes eval/testset.jsonl (committed)
python3 -m dataset.generate --n 24000 --val 2000   # writes data/{train,val}.jsonl
python3 -m eval.run                      # rule-parser baseline
python3 -m eval.run --show-fails         # dump mismatches
```

## Metrics & targets (POC spec, adapted JSON->DSL)

`valid_syntax` (parses under grammar.lark) >= 95%, `valid_command` >= 90%,
`exact_match` >= 80%, latency < 500ms.

Note `valid_syntax` sits a few points below 100% even for correct output: the
`select_all_clips` intents emit `.clips.select()` with no condition, which the
shipping CFG and interpreter both reject. Pre-existing bug, see findings.md.

The no-model regex baseline scores **58.8%** exact in-distribution and **4.3%**
on the held-out dev set — it was hand-fit to the original template set and the
instruction surface has since tripled.

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
python3 -m dataset.generate --n 24000 --val 2000 --langs en,ja,ru,zh

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
