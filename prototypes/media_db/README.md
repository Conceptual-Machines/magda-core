# Media Database — Prototype

Python prototype for MAGDA's media database (issue #768). Indexes audio
samples, presets, and clips with CLAP semantic embeddings and deterministic
audio features. Validates the model choice, label taxonomy, schema, and
throughput before C++ integration via ONNX Runtime.

## Why this exists

Issue #768 stages the work: prove the approach in Python, lock the schema and
model, then port to C++. **Everything in this prototype is designed to port
1:1 to the C++ runtime** — see [`docs/cpp-portability.md`](docs/cpp-portability.md)
for the contract.

## Install

```bash
cd prototypes/media_db
uv sync
```

This installs `transformers`, `torch`, `librosa`, `soundfile`, `typer`, etc.
First run also downloads the CLAP weights (~1.8 GB for `larger_clap_music`).

## Use

```bash
# Initialize an empty DB
uv run media-db init data/media.db

# Index a directory of samples (audio + features + CLAP embeddings + tags)
uv run media-db scan ~/Music/Samples --db data/media.db

# Skip CLAP, just compute deterministic features
uv run media-db scan ~/Music/Samples --db data/media.db --no-embed

# Search semantically + filter
uv run media-db query "warm analog pad" --db data/media.db --kind audio --limit 10
uv run media-db query --kind audio --bpm 120-130 --db data/media.db

# DB stats
uv run media-db stats --db data/media.db

# Export CLAP to ONNX with parity check vs PyTorch (the C++ portability gate)
uv run media-db export-onnx --out models/
```

## Model choice

Default: **`laion/clap-htsat-unfused`** (HTSAT audio encoder + RoBERTa text
encoder, 512-dim, MIT license). Picked after testing all three LAION variants
on 150 random Splice samples:

| Model | Size | Verdict |
|---|---|---|
| `laion/clap-htsat-unfused` | ~600 MB PyTorch / 590 MB ONNX (112 audio + 478 text) | **Default.** logit_scale_a.exp() = 18.7. Ranks piano→"a piano" #1, hi-hat→"a hi-hat" #1, real signal 0.3-0.6 vs noise floor 0.0-0.1. The 478 MB is the standard RoBERTa text encoder — same across all CLAP variants. |
| `laion/larger_clap_general` | ~1.8 GB | Slightly better separation (logit_scale 38.7), but 3× the binary cost for marginal quality. Worth re-testing if quality complaints arise. |
| `laion/larger_clap_music` | ~1.8 GB | **Rejected.** logit_scale_a.exp() = 1.03 — the contrastive head appears untrained. Pairwise audio cosine 0.95+ across diverse files: every sample maps to nearly the same vector. LAION ships this for music-similarity (audio↔audio retrieval), not zero-shot audio↔text — using it for tagging produces 0% tag firing. |

Microsoft MS-CLAP is rejected: CC-BY-NC license, can't ship in MAGDA.

## Tag firing on the validation corpus

Using a 150-file random sample across 62 Splice packs:

- 131/150 files (87%) got at least one tag above confidence threshold 0.15
- Top firing tags: synth pluck, warm sound, synth lead, strings, synth pad, vocal chop, brass, arpeggio, electric guitar, sound effect — all reasonable for a sample-pack corpus
- Common confusions: piano vs organ vs electric piano (acceptable), guitar vs piano (some), trap-specific content not well represented in training data
- Throughput: 5.2 files/sec on Apple Silicon MPS

## Schema

Three tables (`media_file`, `media_embedding`, `media_tag`) plus optional
`media_metadata`. Vectors are stored as raw little-endian float32 blobs so
the C++ side reads them with `std::memcpy`. Full schema in
[`src/media_db/schema.sql`](src/media_db/schema.sql).

## Exit criteria (from issue #768)

- [x] CLAP variant chosen on real sample libraries — `laion/clap-htsat-unfused`
- [x] Label set that fires reliably — 87% file coverage at threshold 0.15
- [x] Throughput measured — 5.2 files/sec on Apple Silicon MPS
- [x] SQLite schema validated against 150 real Splice files
- [x] Model size known — 590 MB ONNX shippable, MIT license
- [x] ONNX export proven with parity check (`export-onnx` command)
- [ ] Packaging strategy for text encoder (478 MB) — bundle vs hosted endpoint
- [ ] Larger-corpus throughput sanity check (current sample is 150 files)

## Tests

```bash
uv run pytest
```

Tests cover schema, file classification, and feature stability on synthetic
signals. They do not load the CLAP model (would download weights in CI);
the `embed` and `query` paths are smoke-tested manually.
