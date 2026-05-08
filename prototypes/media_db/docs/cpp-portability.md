# C++ portability contract

This prototype is a research tool. The artifact that ships with MAGDA is the
C++ runtime, which reads from the same SQLite DB this prototype writes. This
document is the **contract** between the two: what each Python piece becomes
in C++, and how to keep them bit-for-bit compatible where it matters.

## What ports identically

These produce byte-identical state across Python and C++. No risk of drift.

| Component | Python | C++ |
|---|---|---|
| SQLite schema | `src/media_db/schema.sql` | Same file, shipped with the app or compiled in via resource. |
| Vector blob layout | `db.pack_vector` writes raw `float32` little-endian | `std::memcpy(out.data(), blob.data(), dim * sizeof(float))` |
| Tag strings | UTF-8 | UTF-8 |
| File-kind classification | `scan.py`'s extension table | Same extension table |
| File identity | `(mtime_ns, size_bytes, xxh64(first 1 MiB))` | xxhash is BSD-2; use `xxhash.h` or include the source |

## What does not port — and the strategy for each

### CLAP inference

**Python** runs `transformers.ClapModel` on the model checkpoint.

**C++** runs **ONNX Runtime** on `clap_audio.onnx` and `clap_text.onnx`,
exported by `media-db export-onnx`. Inputs/outputs:

- `clap_audio.onnx`: input `input_features` (mel spectrogram tensor, shape
  matches HuggingFace's `ClapProcessor` output); output `embedding` (`float32[batch, 512]`,
  L2-normalized).
- `clap_text.onnx`: inputs `input_ids` and `attention_mask` (RoBERTa tokens);
  output `embedding` (`float32[batch, 512]`, L2-normalized).

**Parity gate**: `onnx_export.py` runs the same input through the PyTorch model
and the ONNX session and asserts max abs diff < 1e-3. **Do not ship a model
that fails this check.** If we want a waveform-in graph (so C++ doesn't need
to compute mel spectrograms), fuse the feature extractor into the export —
left as a follow-up once the model is locked, since it's a non-trivial torch
trace.

**Backends**: CPU first (portable). Once correct: CoreML on macOS via the
ONNX Runtime CoreML EP, DirectML on Windows.

**Tokenizer**: RoBERTa BPE tokenizer. C++ side needs this to embed text. The
options are
1. Bundle the tokenizer config and use `onnxruntime-extensions` to do
   tokenization inside the graph (cleanest).
2. Use a C++ tokenizer library that loads the same `tokenizer.json`
   (e.g. HuggingFace's `tokenizers` Rust lib via FFI, or a JSON-driven BPE
   impl).

Option 1 is preferred — eliminates parity risk on tokenization.

### Deterministic audio features

**Python** uses librosa for BPM, key, RMS, spectral centroid, transient
density. **C++ does not depend on librosa.** Each feature has a documented
algorithm in `features/audio_features.py`; the C++ team reimplements using
JUCE's FFT and standard DSP primitives.

| Feature | Algorithm | C++ source of truth |
|---|---|---|
| duration / sr / channels | from file header | JUCE `AudioFormatReader` |
| RMS | `sqrt(mean(x²))` | trivial |
| spectral centroid | `sum(f·|S|) / sum(|S|)` over STFT frames, mean | JUCE FFT + reduction |
| BPM | onset envelope autocorrelation, peak picking | port librosa's `beat_track` core, or use Tracktion's existing tempo detector if suitable |
| key | mean chroma → Krumhansl-Schmuckler profile correlation | constant-Q or filter-bank chroma + dot product with the two 12-vector profiles in `audio_features.py` |
| transient density | onsets per second from spectral flux peaks | `len(onsets) / duration` |

Feature parity between Python and C++ is **not required to be exact**. What
matters is:
- Same units (BPM in floating-point bpm, centroid in Hz, density in onsets/sec).
- Comparable order of magnitude (within ~10%) so search filters work the same.
- Same null behavior (return NULL for silence, undetected key, etc.).

If a user adds a sample in C++ to a DB that was populated by Python (or vice
versa), the search results should be roughly the same. A 1-BPM difference on
the same file across the two implementations is acceptable; a 60-BPM
difference is not.

### Tag scoring

Same prompts, same model, same cosine similarity. Tag list is shared between
Python and C++ (one source of truth — ship as YAML/JSON resource).

### Vector search

Prototype: brute-force cosine in NumPy. Fine up to ~100k samples.

C++: use [sqlite-vec](https://github.com/asg017/sqlite-vec) extension or an
HNSW index (e.g. hnswlib) loaded alongside the DB. Pick when corpus size
demands it — defer until the prototype validates the rest.

## Schema versioning

`PRAGMA user_version` carries a single integer. Bump it in both Python and
C++ when the schema changes. Migrations are forward-only in the prototype;
the C++ runtime should handle the same migrations.

## Open questions for the C++ port

- Which feature-extraction lib (or in-house DSP) does the C++ side use? Tracktion
  already has an onset/tempo path — worth surveying before reinventing.
- Should we ship sqlite-vec or roll our own brute-force on hot paths? Depends on
  expected corpus size per user; needs a number.
- Background indexing: thread + queue in C++, separate from the audio thread.
  No realtime constraints, but the UI should not block on scans.
