---
license: mit
base_model: microsoft/deberta-v3-base
language:
  - en
library_name: onnx
tags:
  - token-classification
  - intent-classification
  - onnx
  - daw
  - music-production
---

# MAGDA command model

Turns a typed request into a [MAGDA](https://github.com/Conceptual-Machines/magda-core)
DAW command, offline and on-device. "put @serum on a new track" becomes
`track(name="", new=true).fx.add(name="<serum>")`, which the DAW executes as one
undoable transaction.

Fine-tuned from `microsoft/deberta-v3-base`. English only.

## Scope: edits, not gestures

The command set covers **editing operations** — creating and naming tracks,
adding devices, building racks, clip and note operations, grouping, selection
by predicate. Deliberately excluded are real-time transport and mixer states
(play/stop, track mute, solo): those have keyboard shortcuts, and a model with
latency is strictly worse than the button. Queries ("what's the tempo") are
excluded too — they are answers, not edits.

Those requests are not merely unsupported; they are trained as an explicit
**abstain** class, so the model returns nothing rather than executing whichever
command happened to be nearest. A closed label set cannot decline unless
declining is itself a label.

Note that clip enable/disable *is* covered: it is a persistent property of the
project, unlike a mixer mute, and picking one clip out by name is exactly the
kind of thing typing beats clicking at.

## What it actually does

**It never writes the command.** It does perception only, in a single forward
pass, and emits two things:

1. which of 39 commands this is (one of which is "none of these")
2. a BIO tag per word saying what role that word plays

```
"can you   drop  @1176   onto the  vocals"
  O   O     O   PLUGIN    O    O  TRACK_NAME     intent = add_plugin
```

Deterministic code then assembles the DSL from those labels. The model cannot
emit malformed output, because it does not emit output — a property a
generative model would have to buy back with constrained decoding.

Plugin references never reach the model as identities: the host rewrites every
`@mention` to an opaque `<alias>` token before tokenization and substitutes the
real one back by position. So an unseen plugin generalizes for free, and the
vocabulary does not grow with the plugin registry.

## Results

Scored as exact-match on the fully rendered command, not on tags.

| | in-distribution | **held-out phrasing** |
|---|---|---|
| previous model (51k-param conv net, from scratch) | 98.2% | **46.5%** |
| **this model** | 100% | **~90%** |

The two columns matter more than the numbers. The in-distribution set is
authored from the same case list the training templates come from, so a high
score there means the model recalls its templates. The held-out set is 68 cases
hand-written from the command list alone, in the register people actually type:
lowercase, filler, contractions, typos. That is the number that predicts
behaviour on input nobody has seen.

The previous model had no language prior — trained from scratch on synthetic
English, it named tracks after whichever word sat in the name position during
training:

```
"can you drop @1176 onto the vocals" -> track(name="Can You Drop @1176")...
"push Keys up to 2 dB"               -> track(name="Push").track.set(volume_db=2)
"the bass needs @pro_q_3"            -> track(name="Bass Needs").track.set(colour="None")
```

Politeness wrappers scored 10%. With a pretrained encoder they score 100%.

## Files

| file | size | what |
|---|---|---|
| `command_model.onnx` | 442 MB | encoder + intent head + slot head, one graph, opset 17 |
| `tokenizer.json` | 8 MB | HF fast tokenizer, including the `<alias>` special tokens |
| `maps.json` | 1 KB | intent and BIO tag id maps |

Inputs are `input_ids` and `attention_mask`, both `int64 [batch, seq]` with
dynamic sequence length. Outputs are `intent_logits [batch, 39]` and
`slot_logits [batch, seq, 20]`.

## Quantization warning

The embedding is int8; **the attention and FFN deliberately are not.**

| build | size | accuracy |
|---|---|---|
| fp32 | 736 MB | 96.5% |
| int8 embedding only (this) | 442 MB | 96.1% |
| int8 everything | 538 MB | **0.9%** |

DeBERTa-v3's disentangled attention is quantization-hostile. Running
`quantize_dynamic` over the whole graph produces a model that loads, runs at
full speed, and emits garbage. If you re-quantize this, re-measure it.

## Usage

Two details will cost you accuracy if you get them wrong.

**Words, not subwords.** The tags are per word, and the deterministic
reconstructor consumes per-word tags. Label the first subword of each word and
read the tag back at that same index. Reading tags positionally shifts every
tag after the first multi-piece word — it still produces plausible-looking
output, so it fails quietly.

**The alias tokens must stay atomic.** `<alias>` and `<alias.param>` are
registered as additional special tokens. If your tokenizer splits them into
`<`, `alias`, `>`, every plugin slot breaks.

```python
from transformers import AutoTokenizer
import onnxruntime as ort, numpy as np

tok = AutoTokenizer.from_pretrained("microsoft/deberta-v3-base", from_slow=True)
tok.add_special_tokens({"additional_special_tokens": ["<alias>", "<alias.param>"]})
sess = ort.InferenceSession("command_model.onnx")

words = ["put", "<alias>", "on", "a", "new", "track"]
enc = tok(words, is_split_into_words=True, return_tensors="np")
intent_logits, slot_logits = sess.run(None, {
    "input_ids": enc["input_ids"].astype(np.int64),
    "attention_mask": enc["attention_mask"].astype(np.int64),
})
```

A reference implementation in C++ (ONNX Runtime plus a hand-written
SentencePiece Unigram tokenizer) is in `magda/agents/command_model_onnx.cpp`
and `unigram_tokenizer.cpp` in the MAGDA repo.

## Training

24,000 template-generated English examples (no LLM paraphrasing in this
build), 4 epochs, lr 3e-5 for the encoder and 1e-3 for the two heads. Checkpoints are
selected on a hand-authored held-out set, never on the reported one — the
val split is drawn from the same templates as training and saturates at 100%
after one epoch, which makes it useless for ranking checkpoints.

## Limitations

- **English only.** The word splitter in front of the model is ASCII-only, so
  non-English input reaches it as an empty sequence. This is a limitation of
  the surrounding pipeline, not the encoder, which is multilingual-capable.
- **Plugin references need the `@` sigil.** `track with @serum` works;
  `track with serum` does not, and degrades into an unrelated command rather
  than failing cleanly.
- **Closed command set.** 38 commands plus an explicit abstain class. Anything
  outside them returns nothing rather than being mapped onto the nearest
  command — but abstain recall is not perfect, so an unusual out-of-scope
  request can still land on a command.
- Sentences where the verb trails its object ("i don't need the perc track
  anymore, delete it") are a known weak spot.

## License

MIT, following `microsoft/deberta-v3-base`.
