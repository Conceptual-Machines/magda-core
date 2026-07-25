# Handoff: ONNX export + C++ backend for the encoder command model

Issue #1847, the work after the two gated numbers. Read `findings.md` first —
specifically "Out-of-distribution measurement + encoder swap" — for why the
model changed and what it is measured against.

---

## TL;DR

Ship the pretrained-encoder command model on device: export it to ONNX, run it
through the already-vendored ONNX Runtime, and lock the C++ to the Python
reference with a parity test the way `tests/test_command_model.cpp` does today.

**Decide the tokenizer question before writing any code.** It is the whole
cost of this task and it changes which model you export (§1).

The model itself is easy. `dataset/tagging.py`, `reconstruct()`, `magda_dsl/`
and the renderer are untouched and already have a working C++ port — the new
work is a subword tokenizer plus swapping one inference call.

---

## 1. The decision: which encoder, and what it costs in C++

Measured on `eval/ood_testset.jsonl` (71 held-out cases):

| encoder | OOD | C++ tokenizer | int8 size |
|---|---|---|---|
| **DeBERTa-v3 base** | **91.5%** | **Unigram — must be written** | ~184 MB |
| RoBERTa base | 83.1% | byte-level BPE — **already exists** | ~125 MB |

`magda/daw/media_db/RobertaTokenizer.{hpp,cpp}` already implements byte-level
BPE against a Hugging Face `tokenizer.json`, shipped and tested for CLAP
(#768). RoBERTa-base would reuse it essentially as-is.

DeBERTa-v3 does not use BPE. Its tokenizer is **SentencePiece Unigram**
(128k vocab, `Sequence` normalizer), which is a different algorithm: Viterbi
max-score segmentation over a scored vocab, plus SentencePiece's
`precompiled_charsmap` normalization. Roughly 250-400 lines of new C++ and a
fiddly normalizer, but not research — the format is documented and
`RobertaTokenizer` establishes the loading pattern and UTF-8 helpers.

**Recommendation: pay for the Unigram tokenizer and ship DeBERTa-v3.** The
8.4-point gap is roughly one wrong command in twelve versus one in six, on the
exact axis #1847 exists to fix. The size difference is irrelevant — models are
on-demand downloads and ~270 MB is acceptable.

If you want the cheap path first, RoBERTa-base at 83.1% is still +36.6 points
over the shipped conv net and needs no new tokenizer. Landing that, then
swapping the tokenizer later, is a legitimate two-step. The ONNX graph, the
C++ backend and the parity harness are identical either way — only the
tokenizer differs.

---

## 2. Current state

Nothing is committed. Branch: `feat/1847-command-model-encoder`.

```
model/net_encoder.py      encoder + intent head + BIO slot head
model/data_encoder.py     subword <-> word alignment (the one real detail)
model/train_encoder.py    fine-tune; selects on dev, prints ood
model/encoder_parser.py   text -> DSL, the reference implementation to port
eval/dev_testset.jsonl    46 hand-authored cases — tune against this
eval/ood_testset.jsonl    71 hand-authored cases — SEALED, report only
```

Checkpoints are gitignored (0.5-1.1 GB each). Reproduce with:

```bash
cd prototypes/command-model-poc
pip install -r requirements.txt
python3 -m model.train_encoder --preset deberta --epochs 4   # ~25 min on an M1
python3 -m eval.run --encoder-model model/artifacts_encoder_deberta --ood --by-intent
```

Presets live in `model/net_encoder.py:PRESETS`.

---

## 3. The contract the C++ must reproduce

This is `model/encoder_parser.py:predict_dsl` — port it exactly. Steps 1, 2, 6
and 7 are **already implemented in C++** in `magda/agents/command_model.cpp`
and stay as they are; only 3-5 are new.

| # | step | Python | C++ today |
|---|---|---|---|
| 1 | word-split the input | `dataset/tagging.py:tokenize` | `command_model.cpp:tokenize` ✅ |
| 2 | collapse `@mentions` to `<alias>` / `<alias.param>` | `data_encoder.py:canon` | `canonToken` ✅ |
| 3 | subword-encode, words pre-split | `data_encoder.py:encode_words` | **new** |
| 4 | run the encoder | ONNX Runtime | **new** |
| 5 | read one tag per word at its **first subword** | `encoder_parser.py:predict_dsl` | **new** |
| 6 | tags -> actions | `tagging.py:reconstruct` | ✅ ported |
| 7 | actions -> DSL | `magda_dsl/dsl.py:render` | ✅ ported |

**Step 5 is the part that silently goes wrong.** Transformers emit one vector
per *subword*; BIO tags are per *word*, and `reconstruct` consumes word-level
tags. `encode_words` returns `first[]`, the index of each word's first subword;
inference reads `argmax(slot_logits[first[i]])` for word `i`. Words truncated
past `MAX_LEN` get `first == -1` and fall back to `"O"`. Get this wrong and the
model looks 60% accurate instead of 91.5% — the tags shift by a word wherever a
word splits into multiple pieces.

Two more contract details:

- `<alias>` and `<alias.param>` are registered as **additional special tokens**
  (`net_encoder.py:ALIAS_TOKENS`) so they stay one token instead of splitting
  into `<`, `alias`, `>`. The exported tokenizer JSON must carry them, and the
  embedding matrix is resized for them — export after `resize_token_embeddings`.
- `canon` preserves case (unlike the conv net's, which lowercased). These
  encoders are cased and capitalisation is real evidence about what is a name.

---

## 4. Export

Write `model/export_onnx.py` mirroring the existing `model/export_cpp.py`. It
should emit, into a versioned directory:

```
command_model.onnx      encoder + both heads, one graph, dynamic seq length
tokenizer.json          HF fast-tokenizer JSON (includes the alias tokens)
maps.json               intents + tags, already written by train_encoder
```

Export notes:

- Inputs `input_ids` `[B, L]` int64 and `attention_mask` `[B, L]` int64;
  outputs `intent_logits` `[B, n_intents]` and `slot_logits` `[B, L, n_tags]`.
  Mark `L` dynamic — commands are short and padding to a fixed 64 wastes most
  of every forward pass.
- Export in **fp32**. `mdeberta`/`deberta` checkpoints ship fp16 and mixing
  dtypes aborts (see findings.md "Two loading gotchas"). `net_encoder.py`
  already forces fp32 at load.
- Quantize to int8 dynamic *after* exporting fp32, and re-score before
  trusting it. The conv-net port found float32 beat quantized (102/102 vs
  101/102, #1827); assume nothing here.
- Opset 17 is what the vendored ORT in `third_party/onnxruntime` expects.

---

## 5. C++ integration

Three existing things to copy rather than invent:

- **`magda/daw/media_db/ClapTextEncoder.{hpp,cpp}`** — the ONNX text-encoder
  pattern: pimpl so `onnxruntime_cxx_api.h` stays out of the public header,
  tokens-in API, `ClapTextEncoderError` on load/inference failure. Model this
  on it directly.
- **`magda/daw/media_db/RobertaTokenizer.{hpp,cpp}`** — how to load a HF
  `tokenizer.json`, plus UTF-8 helpers you will need for Unigram.
- **`magda/daw/media_db/SampleTaggerDownloader.hpp`** +
  `MediaDbContext::modelsDir()` — the on-demand model download path, with a
  manifest, progress callbacks, size and hash checks. A 184 MB model does not
  go in `resources/models/` next to `basic_pitch.onnx`; it downloads.

Add the new backend **alongside** the current one, don't replace it. The conv
net stays until #1848 settles whether the FPGA track keeps it, and having both
runnable is what let this comparison happen at all.

---

## 6. Parity harness

`tests/test_command_model.cpp` currently locks the conv net to the Python
reference over 111 committed cases. Do the same for the encoder:

1. Extend `export_onnx.py` to dump `(input, expected_dsl)` for every row of
   `eval/testset.jsonl` **and** `eval/ood_testset.jsonl`, scored by the Python
   model — 185 cases.
2. New fixture header alongside `command_model_parity_cases.hpp`.
3. New `TEST_CASE` asserting byte-identical DSL.

Also port the tokenizer probe test added in this session
(`"Command model tokenizer mirrors the Python reference"`). The 111-case
fixture contains no glued-unit input, so it could never catch tokenizer drift —
that is exactly the hole the probe test fills, and a subword tokenizer has far
more room to drift than the word regex did.

**Expect parity to be harder than it was for the conv net.** That port was
integer vocab lookup plus small float convolutions. This one has a 128k-entry
Unigram vocab with float scores, a Unicode normalizer and a Viterbi search, any
of which can disagree with Python on an edge case that never appears in 185
cases. Fuzz the tokenizer against the Python one over a few thousand random
strings before trusting the parity number.

---

## 7. Known gaps, deliberately left

- **`.clips.select()` is broken.** `command_model.cpp:688` emits a bare
  `.clips.select()` for `select_all_clips` / `select_all_clips_rename`, but the
  shipping grammar and `dsl_interpreter.cpp:executeSelectClips` both require a
  condition. Seven in-distribution and two OOD gold rows are DSL the
  interpreter rejects. Pre-existing; wants its own ticket. It will show up as
  parity-passing-but-non-executable, so don't be confused by it.
- **Reconstructor ceiling is 98.6%, not 100%.** One OOD case ("track with
  serum on it") needs bare plugin names resolved against the registry, which
  was deliberately not done — it would rewrite a track legitimately named
  "Serum". Product question, not a bug.
- **English only.** The word tokenizer is ASCII-only, so non-English input
  reaches the model as an empty sequence. Multilingual plumbing was removed
  (2026-07-25). #1846 starts at the tokenizer, not the model — see the
  multilingual section in `findings.md`.

---

## 8. Done when

- [x] Tokenizer decision made — **DeBERTa-v3**, Unigram written by hand
- [x] `model/export_onnx.py` produces onnx + tokenizer.json + maps.json
- [x] ONNX output matches the PyTorch forward — 231/231 identical DSL
- [x] C++ tokenizer fuzz-matches the Python tokenizer — 3330/3330 words
- [x] ORT-backed backend beside the existing one (`command_model_onnx.hpp`)
- [x] Parity test over all 231 committed cases, byte-identical
- [ ] Wire into the app: download flow + `FAST_INFERENCE` provider selection
- [ ] Decide the shipping artifact (see §9)

## 9. What is left, and the one open question

Everything through the parity test is done and green (`make test`: 1523 cases).
Two things remain.

**Wire it into the app.** `CommandModelOnnx` compiles and passes parity but
nothing constructs it yet. It needs the download flow (§5) and a provider
choice next to the existing `FAST_INFERENCE` path. `isInstalled()` is there so
the UI can fall back to the conv net when the assets are absent.

**The artifact is bigger than planned.** int8 is not available here:

| build | size | held-out accuracy |
|---|---|---|
| fp32 | 736 MB | 96.5% on the combined 231 |
| int8, embedding (Gather) only | 442 MB | 96.1% |
| int8, attention/FFN (MatMul) | 538 MB | **0.9% — destroyed** |

DeBERTa-v3's disentangled attention is quantization-hostile; this is a known
property of the architecture, not a bug in the export. So the realistic
shipping options are 442 MB (int8 embedding, fp32 body) or an fp16 body, which
should land near 270 MB — the fp16 conversion currently fails with a type error
at the embedding Cast node and needs `op_block_list` tuning. That is the first
thing to try if 442 MB is too much.

If it has to be smaller than fp16 allows, the answer is RoBERTa (83.1%, and it
quantizes normally) or distilling the fine-tuned DeBERTa into a student — not
naive int8 on this graph.
