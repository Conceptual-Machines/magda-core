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

Default: **`laion/larger_clap_music`** (HTSAT audio encoder + RoBERTa text
encoder, 512-dim, MIT license). Alternates worth comparing during the
prototype phase:

| Model | Size | Notes |
|---|---|---|
| `laion/larger_clap_music` | ~1.8 GB | Music-tuned, primary candidate. |
| `laion/larger_clap_general` | ~1.8 GB | General audio, better for FX/ambiences. |
| `laion/clap-htsat-unfused` | ~600 MB | Smaller baseline; quality/size tradeoff. |

Microsoft MS-CLAP is rejected: CC-BY-NC license, can't ship in MAGDA.

## Schema

Three tables (`media_file`, `media_embedding`, `media_tag`) plus optional
`media_metadata`. Vectors are stored as raw little-endian float32 blobs so
the C++ side reads them with `std::memcpy`. Full schema in
[`src/media_db/schema.sql`](src/media_db/schema.sql).

## Exit criteria (from issue #768)

- [ ] CLAP variant chosen on real sample libraries
- [ ] Label set that fires reliably (current default in `tags.py`)
- [ ] Throughput per CPU core measured
- [ ] SQLite schema locked
- [ ] Model size, license, packaging strategy confirmed
- [ ] ONNX export proven with parity check (`export-onnx` command)

## Tests

```bash
uv run pytest
```

Tests cover schema, file classification, and feature stability on synthetic
signals. They do not load the CLAP model (would download weights in CI);
the `embed` and `query` paths are smoke-tested manually.
