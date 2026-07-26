# Spike: MAGDA tiny command model -> FPGA

Where this stands and why, so the next session doesn't re-litigate decisions.
This `command-model-poc/` folder is the spike; the code and this doc live here.

Read in two passes. Everything up to "Hardware decision" is the original
from-scratch conv net built for an FPGA target (status 2026-06-13). The section
"Out-of-distribution measurement + encoder swap" (2026-07-25, #1847) is the
correction: that model scores 46.5% on held-out phrasing, and the decisions
below it — English-only, ~100-token vocab, convs over attention — are all
downstream of the FPGA constraint, which does not apply to the CPU product.
Decisions 1 and 3 (emit the existing DSL; classify + deterministically
reconstruct rather than generate) survive that correction unchanged and are
load-bearing for both paths.

## Goal (as it actually evolved)

Natural-language DAW automation ("create a bass track with serum and ott") ->
validated MAGDA command DSL, executed through the EXISTING apply/undo pipeline.
The ultimate target is **running inference on an FPGA** (not just local/offline)
- that constraint was not in the original spec and reshaped the whole approach.

Out of scope (handled by other agents): music generation, mixing, per-parameter
DSP, creative decisions.

## Locked decisions (with rationale)

1. **Emit the EXISTING DSL, not new JSON.** The shipping `CommandAgent` already
   produces a functional chained DSL (`magda/agents/dsl_grammar.hpp`), CFG-
   constrained, wired to `InstructionExecutor` + `UndoManager`. We target that
   grammar so anything produced is already executable. (Most of the spec's
   "apply/preview/undo" already exists in the repo.)

2. **English-only.** Multilingual was explored (en/ja/ru/zh) then dropped for the
   FPGA target: a multilingual tokenizer's embedding table (~150k vocab) is the
   thing that does NOT fit on-chip. English collapses the vocab to ~100 tokens.
   ~~Multilingual code is PARKED behind flags~~ — **removed** 2026-07-25, along
   with the 45 non-English training rows. They were training on empty token
   sequences (see "Multilingual is NOT free" below); the FPGA rationale turned
   out to be the wrong reason for a decision that was right anyway.

3. **Intent + slot tagging, NOT generative seq2seq.** This is the pivotal call.
   A generative model emits DSL token-by-token = an autoregressive loop with KV
   cache + sampling + feedback - exactly what FINN/hls4ml/FPGA dataflow do NOT
   do. Because the instruction set is FIXED (closed command set), we don't need
   generation: classify the intent + tag each input word with its slot role in a
   SINGLE forward pass. A deterministic reconstructor (`dataset/tagging.py:
   reconstruct`) + `dsl.render` rebuild the exact DSL on the CPU side. The net
   never emits DSL -> cannot produce malformed DSL (correctness guarantee) AND
   streams cleanly on FPGA.

4. **Values are generic spans, not understood.** Names, plugins, colours, ids,
   pan words, and numeric values are tagged as spans; the model is blind to
   their content. The ARM parses "-6 dB" -> `volume_db=-6`, or "hard right" ->
   `pan=1`. The parameter identity comes from the INTENT, so a single generic
   VALUE slot suffices for single-value commands. Only MULTI-value utterances
   need role-typed value slots.

## What's built and working

Pipeline (all local, M1):
- `magda_dsl/` - grammar (mirrored from dsl_grammar.hpp), canonical renderer,
  vocab (plugins/colours), lark validator. All gold DSL parses under the real CFG.
- `dataset/generate.py` - template synthetic data, leakage-guarded train/val
  split (train ~21000 / val ~2900 / test 102, zero overlap), plus optional
  LLM-teacher paraphrase augmentation (`--teacher`, DeepSeek, cached).
- `dataset/tagging.py` - (input, actions) -> (intent, BIO tags); round-trip
  reconstruct -> render == gold = **95.5%** (the ~4.5% is cosmetic name casing,
  e.g. "FX" vs "Fx").
- `baseline/rule_parser.py` - Phase-1 no-model floor: 100% on the English test set
  (but it's hand-fit regex; doesn't generalize - that's the point of the model).
- `model/` - Brevitas QAT intent+slots model + training + eval parser.

### Instruction set (expanded, 2026-07-24)

33 intents. Beyond the original track/clip-selection ops: clip ops (new/rename/
delete), track move, MIDI note ops (delete, transpose, set-velocity, resize,
quantize, set-pitch, select-by-pitch, select-by-velocity), and groove (set/list).

### Plugin references: opaque placeholders, resolved off-model

The model NEVER learns plugin identities. A host preprocess step (`model.data.
canon`) rewrites every plugin @mention -> one opaque `<alias>` token, and any
plugin-param reference -> a single opaque `<alias.param>` token, BEFORE the model
sees it. The model only tags the placeholder's position/role; the reconstructor
substitutes the real alias/param back by position (1st @mention -> its alias,
1st PARAM span -> param1, ...). Consequences: an unseen alias generalises for
free (it's indistinguishable from a known one), and the model vocab never grows
with the plugin registry. Data files keep the readable `@serum` surface so the
tagger/reconstructor can still recover the true identity.

### Model result (expanded instruction set, LLM-teacher data)

| metric | value |
|---|---|
| params | **~124,500** |
| size @ 4-bit weights | **~60.8 KB** |
| val end-to-end exact | 94.4% |
| **held-out test exact** | **99.0%** (syntax 100%, command 100%, n=102) |
| latency | ~1.9 ms (CPU/M1 PyTorch; FPGA will be ~us, deterministic) |

The param/size jump vs the first run (43,782 / 21.4 KB) is the embedding table:
LLM-teacher paraphrases push the vocab to ~1780 tokens (free-form English), vs
the ~100-token target of the original all-English design. This is a real
teacher-diversity <-> on-chip-embedding trade to revisit before FINN export (move
the embedding to a host lookup / on-chip ROM, or prune the teacher vocab). Still
trivially on-chip on the KV260.

The one held-out miss is `rename track Bass to Reese Bass` — the old name is a
substring of the new one, a shape `gen_rename_track` deliberately EXCLUDES to
keep span tagging unambiguous, so the model never trains on it. Valid DSL, wrong
old-name span. Open call: allow substring-name pairs in training, or leave it.

Architecture (`model/net.py`): embedding -> 3 dilated 1D-conv blocks (QuantConv1d
+ QuantReLU, RF ~15 tokens) -> intent head (masked mean-pool) + slot head
(per-token). Convs chosen because FINN streams them natively; no recurrence/
attention. OOV-name augmentation (`unk_aug`) forces tagging by context so unseen
track names generalize.

The fixed English test set currently has no misses after the volume/pan,
clip-selection, and select-then-rename refresh. Remaining risk is generalisation
outside the template distribution, especially more free-form numeric,
stereo-field, clip-selection, and chained-operation phrasing. Numbered rename
templates like `{i}` need tokenizer support before they should be fixed-eval
targets.

## Out-of-distribution measurement + encoder swap (2026-07-25, #1847)

### The number above was measuring the wrong thing

`eval/testset.jsonl` is hand-authored from the same case list `dataset/
generate.py` builds its templates from. Test set and training data are two
renderings of one source, so a high score there means "the model recalls its
templates" and says nothing about unseen phrasing. It had never been measured
out of distribution.

`eval/ood_testset.jsonl` (new, committed) fixes that: 71 cases covering all 38
intents, written from the intent list and DSL semantics only — never from the
template file — in the register people actually type in (lowercase, filler,
contractions, the verb that came to mind, occasional typos).
`eval/make_ood_testset.py` asserts no case appears verbatim in train/val/test
and reports the token-level Jaccard to the nearest training row (mean 0.53,
max 0.90, zero token-identical), so "out of distribution" is a measured
property rather than a claim. Each case carries `ood_tags` naming the pressure
it applies, and `eval.run --by-intent` slices exact-match by intent and by tag.

### Results

All encoders: same 24k rows, same hyperparameters (3 epochs, lr 3e-5 encoder /
1e-3 heads, batch 32). "OOD" is the checkpoint model selection actually kept;
see the selection section below for why that is lower than the best epoch.

Final runs select on `dev_testset.jsonl`; the earlier val-selected runs are kept
below for the selection comparison.

| model | params | int8 | **OOD** | selected on |
|---|---|---|---|---|
| conv net (shipped, `model/artifacts`) | ~51k | 26 KB | **46.5%** | — |
| **DeBERTa-v3 base (English)** | 184M | ~184 MB | **91.5%** | dev |
| mDeBERTa-v3 base | 278M | ~278 MB | 90.1% | dev |
| XLM-R base | 278M | ~278 MB | 87.3% | dev |
| RoBERTa base (English) | 125M | ~125 MB | 83.1% | dev |
| DistilRoBERTa (English) | 82M | ~82 MB | 81.7% | dev |
| distilmBERT | 135M | ~135 MB | 80.3% | val (blind) |
| MiniLM multilingual | 118M | ~118 MB | 77.5% | val (blind) |

**DeBERTa-v3 base is the pick**: 91.5% against 46.5% shipped — a 45-point gain
on input nobody has seen — and it beats the multilingual model of the same
family at two-thirds the size. Dropping languages the tokenizer could not serve
anyway bought accuracy rather than costing it. What remains is ~7 points of
model error against a 98.6% reconstructor ceiling.

**The dev set now needs to be bigger.** 46 cases means 97.8% = 45/46, and
epochs 2 and 3 tied there while differing by 1.5 points on OOD (91.5 vs 93.0).
Selection keeps the first of a tie, so it kept the 91.5% one. That is a much
milder failure than val's — dev ranks checkpoints, it just cannot separate the
top two — but it is the same shape, and the fix is the same: more hand-authored
cases. Do NOT fix it by preferring the later epoch on ties; that rule was
chosen by looking at OOD, which is precisely what the sealed set is for
avoiding. ~100 dev cases would resolve it honestly.

Two notes on the smaller English models. RoBERTa and DistilRoBERTa are not
undertrained — both drive training loss to ~0.002 and plateau, so the gap is
capacity and architecture, not optimisation. DeBERTa-v3's disentangled
attention encodes relative position separately from content, which suits span
tagging specifically ("which word is the name" is a positional question), and
its ELECTRA-style pretraining is markedly more sample-efficient per parameter.
If size ever becomes binding, the move is distilling the *fine-tuned* DeBERTa
into a student, not starting from an off-the-shelf distilled encoder.

**Size is not a live constraint.** Local models are optional on-demand
downloads, not bundled in the app; ~270 MB is acceptable. So the encoder is
chosen on accuracy, and the distilled candidates are recorded rather than
decisive. The 51k conv net was small because the FPGA target required fitting
on-chip — carrying that constraint into the CPU product is precisely what
produced a model with no language prior.

The shipped model is wrong more often than right on ordinary input. The failure
signature is diagnostic, not incidental — it gets the operation and the value
right, then names the track after whatever word occupied the name position
during training:

```
"can you mute the guitar"     -> track(name="Can You").track.set(mute=true)
"push Keys up to 2 dB"        -> track(name="Push").track.set(volume_db=2)
"the bass needs @pro_q_3"     -> track(name="Bass Needs").track.set(colour="None")
```

That is the "no language prior" claim with a number on it: the net learned
template positions, not English. Sliced by OOD pressure, the encoder swap lands
precisely where the theory says it should:

| pressure | conv net | XLM-R |
|---|---|---|
| filler ("can you…", "i want…") | 10% (1/10) | **100%** |
| postposed ("@surge_xt on it") | 0% (0/2) | **100%** |
| typo | 33% | **100%** |
| unnamed ("track with @fm_0") | 25% | 88% |
| colloquial verb | 51% | 83% |

### The reconstructor ceiling: 95.8% -> 98.6%

Feeding the *gold* intent and tags straight into `reconstruct` + `dsl.render`
reproduced the gold DSL on only 68/71 — so ~4 points of the OOD gap belonged to
the deterministic side, where no encoder could reach them. Two are now fixed:

- `"drop the bass notes 12 semitones"` — `reconstruct` negated only on
  "down"/"lower", so "drop" yielded +12. **Fixed**: drop/dropped/below join the
  downward set.
- `"bring the bass down to -6db"` — `tokenize` kept `-6db` as one token, so the
  value never separated from its unit. **Fixed**: `_split_glued_units` splits a
  number from a trailing unit, restricted to a `db|bars?|beats?|semitones?|st`
  whitelist. A general digits-then-letters split would wreck `16ths` (a grid
  phrase), `C3` (a pitch) and `@pro_q_3` (an alias).
- `"track with serum on it"` — a bare plugin name with no `@` sigil. **Left
  alone deliberately.** Resolving it means matching raw words against the
  plugin registry inside `canon`, which would rewrite a track legitimately
  named "Serum". The console offers `@` autocomplete; bare names are an
  alias-resolution product question, not a model defect.

Ceiling is now 98.6% on the OOD set and 100% on the dev, in-distribution and
val sets. The two fixes lift the *shipped conv net* from 46.5% to **49.3%**
with no retraining, which is the point: they pay off under any encoder.

### Model selection is currently blind — fix before tuning

Val saturates immediately — it is drawn from the same templates as train — so
it cannot rank checkpoints that differ by 4-6 points on held-out phrasing:

| run | val by epoch | **OOD by epoch** | kept |
|---|---|---|---|
| XLM-R | 100 / 100 / 100 | 84.5 / **88.7** / 84.5 | epoch 1 (84.5) |
| distilmBERT | 99.5 / 100 / 100 | 81.7 / 80.3 / **85.9** | epoch 2 (80.3) |
| MiniLM | 99.8 / 100 / 99.8 | 80.3 / 77.5 / **81.7** | epoch 2 (77.5) |
| mDeBERTa | 100 / 100 / 100 | **90.1** / 88.7 / 88.7 | epoch 1 (90.1) |

Three bad picks out of four, wrong in both directions. XLM-R kept the first
checkpoint to tie at 100% and left 4.2 points behind. distilmBERT and MiniLM
each *upgraded to a worse model* on a val tick of half a point, then discarded
their best epoch. mDeBERTa landed on its best epoch — by the same
first-to-tie-at-100% rule that cost XLM-R 4.2 points, so that is luck, not
signal.

This is decision-relevant, not cosmetic. Size is not the constraint (see
above), so the encoder is picked on accuracy — and the epoch-to-epoch spread
(4-6 points) is wider than the gap between the candidates. Without a
selection set we cannot reliably tell XLM-R from mDeBERTa, or epoch 2 from
epoch 3, which is the entire remaining question.

The numbers above are the checkpoints selection actually chose, because those
are the honest ones. Selecting on the OOD set instead would spend it: the score
would then describe those 71 sentences rather than predict the 72nd, which is
the 111/111 self-deception one level up.

**Fixed.** `eval/dev_testset.jsonl` (46 cases) is a second hand-authored set,
built the same way and asserted disjoint from train, val, `testset.jsonl` and
`ood_testset.jsonl`. `train_encoder.py --select-on dev` (now the default) picks
checkpoints against it and only *prints* the OOD score. `--select-on val`
reproduces the old blind behaviour.

It works. Retrained over 4 epochs with dev selection:

| run | val by epoch | dev by epoch | ood by epoch | kept |
|---|---|---|---|---|
| mDeBERTa | 100 / 100 / 100 / 100 | 87.0 / **89.1** / 84.8 / 82.6 | 88.7 / **90.1** / 87.3 / **90.1** | epoch 2 (ood 90.1) |
| XLM-R | 100 / 100 / 99.4 / 100 | **84.8** / 78.3 / 82.6 / **84.8** | **87.3** / 84.5 / **87.3** / 84.5 | epoch 1 (ood 87.3) |

Dev picked a joint-best checkpoint in both runs. Val stayed pinned at 100%
throughout both, confirming it carries no signal whatsoever — 8 epochs of
training, 8 identical scores, while the real number moved 5.6 points.

### Multilingual is NOT free — but the encoder is not the blocker

#1847 expects multilingual "for free" from XLM-R, likely superseding #1846
(language packs). Measured on the 45 curated ja/ru/zh seeds — which are *in the
training data*, so this is the friendliest possible test:

| | ja | ru | zh |
|---|---|---|---|
| conv net | 13.3% | 20.0% | 20.0% |
| mDeBERTa-v3 | 13.3% | 13.3% | 13.3% |

The multilingual encoder is no better than the English-only conv net, because
neither ever sees the input. `dataset/tagging.py:_TOK` is
`[@#]?[A-Za-z0-9_'\-]+(?:\.[0-9]+)?` — **ASCII-only**:

```
"ベーストラックにSerumを追加"  -> ['Serum']
"создай басовую дорожку"      -> []
"创建一个贝斯轨道"              -> []
```

Cyrillic and CJK tokenize to *nothing*. The model is handed an empty sequence
and the score is whatever the reconstructor's fallbacks produce by luck. The 45
non-English training rows were likewise training on empty inputs.

So the encoder swap neither proves nor disproves the multilingual claim. The
blocker sits in front of the model: word segmentation exists because BIO tags
are per-word and `reconstruct` consumes word-level tags, and that segmenter is
English-shaped. Two very different fixes:

- **Russian (and any whitespace-delimited script): cheap.** Make `_TOK`
  Unicode-aware. Needs the same change mirrored in `command_model.cpp` and a
  retrain, since the vocab changes.
- **Japanese/Chinese: not cheap.** No whitespace, so "one tag per word" has no
  natural unit. Either segment per character, or drop word-level BIO for
  subword-level tagging and rewrite `reconstruct`'s span logic. That is a
  design change, not a regex fix.

Latin-script languages sit between the two: the ASCII regex handles unaccented
words but fragments every accented one (`añade` -> `a`,`ade`; `crée` ->
`cr`,`e`; `füge` -> `f`,`ge`; `criação` -> `cria`,`o`). A Unicode-aware `_TOK`
fixes es/fr/de/it/pt/nl **and** ru in one change — verified against samples.

**Decision (2026-07-25): English only.** The multilingual plumbing is deleted,
not parked — `i18n.py`, `--langs`, `INCLUDE_NON_EN`, and the 45 non-English
training rows. Working English beats broken everything, and none of the removed
code was doing anything but feeding the model empty sequences. #1846 is not
superseded by the encoder swap; it starts at the tokenizer.

### Two loading gotchas (both fixed in `net_encoder.py`)

**Sentencepiece tokenizers.** `mdeberta-v3-base` and `Multilingual-MiniLM` both
fail to load under transformers 5.14: the loader misreads their `spm.model` /
`sentencepiece.bpe.model` as a tiktoken file and raises. `from_slow=True`
converts it properly — `load_tokenizer` retries with that. A *fast* tokenizer
is mandatory either way, because the subword/word alignment needs `word_ids()`.
`xlm-roberta-base` is unaffected only because HF ships a pre-converted
`tokenizer.json` for it.

**fp16 checkpoints.** `mdeberta-v3-base` ships fp16 weights and transformers 5.x
honours that, so its matmuls meet the fp32 heads and abort — as a dtype
mismatch on CPU, and as a bare Metal assertion on MPS ("Destination NDArray and
Accumulator NDArray cannot have different datatype") that looks like an
architecture incompatibility but is not. `AutoModel.from_pretrained(...,
dtype=torch.float32)` fixes both.

### The one real implementation detail, as predicted

Transformers tokenize to subwords; `dataset/tagging.py` emits one BIO tag per
word. `model/data_encoder.py` labels the first subword of each word and masks
the rest, and reads tags back at the same positions at inference — so
`reconstruct()` still receives word-level tags and is untouched. Word
segmentation stays `dataset.tagging.tokenize`, so a word index means the same
thing on both sides. `<alias>` / `<alias.param>` are registered as additional
special tokens so the plugin placeholders stay atomic rather than splitting
into `<`, `alias`, `>`.

`canon()` is retained (plugin identities still never reach the model) minus the
`.lower()`, which only ever existed to keep the from-scratch model's 291-word
vocab small. These encoders are cased, and capitalisation is real evidence
about what is a name.

### Grammar mirror had drifted (fixed)

`magda_dsl/grammar.lark` claims to mirror `dsl_grammar.hpp` verbatim but
predated the `rack.*` methods, so every `create_rack` gold scored as a syntax
error. Re-synced. That exposed drift in the other direction too: the shipping
CFG *and* the interpreter (`dsl_interpreter.cpp: executeSelectClips`) require a
condition inside `clips.select(...)`, but `command_model.cpp:688` emits a bare
`.clips.select()` for `select_all_clips` / `select_all_clips_rename`. Seven
in-distribution and two OOD gold rows are DSL the interpreter rejects. Real
bug, pre-existing, not fixed here — the stale mirror was hiding it.

## Hardware decision

- **Board: AMD Kria KV260 Vision AI Starter Kit (DigiKey SK-KV260-G).** Zynq
  UltraScale+ SoC: quad A53 (runs tokenizer + reconstruct + render + I/O via
  Kria-PYNQ) + fabric (runs the net via FINN) + URAM (on-chip headroom).
  Also a productizable SOM (K26). Pynq-Z2 was the budget alternative.
- **Still to buy:** microSD (16GB+, boots Kria-PYNQ), Ethernet cable, USB cable.
  PSU is included in the kit.
- **Build host:** the user's x86 Linux box (x86-64, 16GB RAM, ~150GB disk -
  confirmed). Runs Vivado/Vitis + FINN-in-Docker. The M1 CANNOT build (Xilinx
  tools are x86-Linux only); M1 stays the eval/latency + dev machine.
- **Gotcha:** pin the Vivado version to the chosen FINN release.

## Training infra (the GGUF teacher path, parallel track)

The generative 0.5B path is NOT the FPGA artifact but stays as the distillation
teacher / accuracy ceiling:
- `model/train_colab.ipynb` - unsloth LoRA on Qwen2.5-0.5B-Instruct, exports GGUF.
- Data staged to Google Drive (`My Drive/magda-command-model/`) for Colab; GGUF
  syncs back via Drive-for-Desktop.
- `eval.run --model X.gguf` scores it on the M1 (llama-cpp-python).
- Qwen is NOT load-bearing: data gen is deterministic templates; an LLM teacher
  is only an optional v2 lever for input paraphrase diversity, and is pluggable.

## Next steps

The CPU product and the FPGA track have separated; these are the CPU ones
(#1847). The FPGA list below them is unchanged and now belongs to whatever
model that track keeps.

1. ~~A selection set.~~ Done: `eval/dev_testset.jsonl`, 46 cases,
   `--select-on dev`.
2. **Retrain the candidates under dev-set selection** and re-report. The runs
   above were selected on val, so their kept checkpoints are arbitrary within a
   4-6 point band; the ranking between XLM-R and mDeBERTa is not yet settled.
3. **ONNX export + an ORT-backed `CommandModel`** alongside the current one.
   ONNX Runtime is already vendored and already ships models
   (`resources/models/basic_pitch.onnx`), so this adds no dependency.
4. **Parity harness** locking C++ to the Python reference, as
   `tests/test_command_model.cpp` does for the conv net.
5. **Decide the conv net's fate** — retire, or keep as the FPGA track. #1848
   proposes retargeting that track at chord/rhythm, which would answer this.
6. **Fix `.clips.select()`** (see "Grammar mirror had drifted"): two intents
   emit DSL the interpreter rejects. Either widen the interpreter or stop
   emitting the no-condition form.

FPGA track (unchanged, now decoupled from the CPU product):

7. **Expand the instruction set** - cheap on the model side (a few output
   classes). Real cost is: (a) extend the DSL grammar/interpreter for the gaps
   (`duplicate_track`, `remove_plugin`, `route_track`) so they're executable;
   (b) templates + tagger updates; (c) numeric/relational slots. Stay in the
   STRUCTURAL-automation domain to preserve the single-pass framing.
8. **FINN export hardening** (on the Linux box once Vivado is in): thread
   QuantTensors end-to-end, move the embedding to a host lookup / on-chip ROM,
   ONNX -> FINN dataflow build.
9. **Deploy** to KV260: FINN bitstream on fabric, PYNQ glue on the A53.

## How to reproduce the current state

```bash
cd prototypes/command-model-poc
pip install -r requirements.txt          # lark, torch, brevitas, transformers
python3 -m eval.make_testset             # in-distribution set (114, en)
python3 -m eval.make_ood_testset         # held-out set (71, en) + novelty check
python3 -m dataset.generate --n 12000 --val 1500 # frozen splits
python3 -m dataset.tagging --demo        # see tags + 95.5% round-trip

# the from-scratch conv net (FPGA shape)
python3 -m model.train_intent_slots --epochs 25   # -> model/artifacts/
python3 -m eval.run --torch-model                 # 98.2% in-distribution
python3 -m eval.run --torch-model --ood --by-intent  # 46.5% held out

# the pretrained encoder (#1847)
python3 -m model.train_encoder --preset xlmr --epochs 3
python3 -m eval.run --encoder-model model/artifacts_encoder_xlmr --ood --by-intent
```

Encoder checkpoints are gitignored (0.5-1.1 GB each); retrain to reproduce.
Presets: `xlmr`, `mdeberta`, `distilmbert`, `minilm`.
