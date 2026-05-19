#!/usr/bin/env python3
"""Upload MAGDA sample-tagger ONNX models to HuggingFace.

Reads the ONNX exports + RoBERTa tokenizer from --models-dir and pushes
them to the configured HF repo. Generates a README.md model card with
provenance, file sizes, SHA-256 hashes, and the BSD-3-Clause notice on
the way in.

Usage:
    HF_TOKEN=hf_... python scripts/upload_sample_tagger_to_hf.py \\
        --models-dir prototypes/media_db/models \\
        --repo-id ConceptualMachines/magda-sample-tagger

Dependencies:
    pip install huggingface_hub
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path
from typing import Dict


EXPECTED_FILES = ["clap_audio.onnx", "clap_text.onnx", "tokenizer.json"]


def sha256_file(path: Path) -> str:
    """Stream-hash so we don't load 500 MB into memory."""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def fmt_size(num_bytes: int) -> str:
    for unit, factor in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if num_bytes >= factor:
            return f"{num_bytes / factor:.1f} {unit}"
    return f"{num_bytes} B"


def build_model_card(repo_id: str, file_meta: Dict[str, Dict[str, object]]) -> str:
    # juce::ignoreUnused-style: repo_id reserved for future links inside the card.
    del repo_id

    table_rows = "\n".join(
        f"| `{name}` | {fmt_size(int(meta['size']))} | `{meta['sha256']}` |"
        for name, meta in file_meta.items()
    )
    # Written flat (no dedent) because the YAML frontmatter --- markers MUST
    # be at column 0 for HuggingFace's parser to pick up license/tags. Any
    # textwrap.dedent / leading-whitespace dance is one rebuild away from
    # silently breaking that.
    return f"""\
---
license: bsd-3-clause
tags:
- audio
- audio-classification
- sample-tagging
- clap
- htsat
- onnx
library_name: onnxruntime
---

# MAGDA Sample Tagger

ONNX exports of [LAION's CLAP HTSAT-unfused model](https://huggingface.co/laion/clap-htsat-unfused)
plus the RoBERTa tokenizer, packaged for the
[MAGDA DAW](https://github.com/Conceptual-Machines/magda-core)'s sample
library (issue #768).

## What's in this repo

| File | Size | SHA-256 |
|------|------|---------|
{table_rows}

- `clap_audio.onnx` — audio encoder. Takes a mono 48 kHz waveform,
  produces a 512-d normalised embedding suitable for cosine similarity
  search.
- `clap_text.onnx` — text encoder. Takes RoBERTa token ids + attention
  mask, produces a 512-d normalised embedding in the same space as the
  audio encoder so a text query can rank audio files by similarity.
- `tokenizer.json` — the RoBERTa BPE tokenizer that pairs with the
  text encoder. MAGDA's C++ tokenizer reads this file directly.

## How MAGDA uses these

MAGDA's media database (a SQLite catalogue of audio samples) uses
these encoders to:

- Compute an embedding per indexed sample at index time, stored in the
  `media_embedding` table.
- Encode the user's free-text search query at query time and rank
  samples by cosine similarity to the query embedding.

Without these models MAGDA falls back to filename / tag full-text
search — still useful, just no semantic similarity.

## Export procedure

ONNX exports are generated from `laion/clap-htsat-unfused` via the
export script in MAGDA's prototype:

```
prototypes/media_db/src/media_db/embeddings/onnx_export.py
```

Notes:

- Run on CPU (MPS does not support float64 used by the audio encoder's
  mel filterbank).
- Requires `transformers >= 5.x`. The audio-feature accessor was renamed
  from `audios=` to `audio=` between 4.x and 5.x; passing the old kwarg
  silently returns wrong shapes.
- `tokenizer.json` is the unmodified file from the upstream HF repo,
  fetched via `AutoTokenizer.from_pretrained(...).save_pretrained(...)`.

## License

BSD-3-Clause — same as the upstream LAION CLAP weights. See the
upstream repo for the original notice and attribution.
"""


def upload(repo_id: str, token: str | None, file_meta: Dict[str, Dict[str, object]],
           model_card: str) -> None:
    # Imported lazily so --dry-run works without huggingface_hub installed.
    from huggingface_hub import HfApi, create_repo

    api = HfApi(token=token)
    create_repo(repo_id, exist_ok=True, repo_type="model", token=token)

    # Push README first so the repo has a model card the moment a viewer lands.
    api.upload_file(
        path_or_fileobj=model_card.encode("utf-8"),
        path_in_repo="README.md",
        repo_id=repo_id,
        repo_type="model",
        commit_message="model card",
    )

    for name, meta in file_meta.items():
        print(f"Uploading {name} ({fmt_size(int(meta['size']))})...", flush=True)
        api.upload_file(
            path_or_fileobj=str(meta["path"]),
            path_in_repo=name,
            repo_id=repo_id,
            repo_type="model",
            commit_message=f"add {name}",
        )

    print(f"\nDone. Repo: https://huggingface.co/{repo_id}")
    print("Direct download URLs (use these in the in-app downloader):")
    for name in EXPECTED_FILES:
        print(f"  https://huggingface.co/{repo_id}/resolve/main/{name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument(
        "--models-dir",
        type=Path,
        required=True,
        help="Directory containing clap_audio.onnx, clap_text.onnx, tokenizer.json",
    )
    parser.add_argument(
        "--repo-id",
        default="ConceptualMachines/magda-sample-tagger",
        help="HuggingFace repo id (default: %(default)s)",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="HF write-token. Falls back to HF_TOKEN env var / cached `huggingface-cli login`.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print model card + planned upload, don't push.",
    )
    args = parser.parse_args()

    missing = [f for f in EXPECTED_FILES if not (args.models_dir / f).is_file()]
    if missing:
        print(f"Missing: {', '.join(missing)}", file=sys.stderr)
        print(f"Expected under: {args.models_dir.resolve()}", file=sys.stderr)
        if "tokenizer.json" in missing:
            print(
                "\nThe RoBERTa tokenizer isn't produced by the ONNX export step.",
                file=sys.stderr,
            )
            print("Fetch it from upstream first:", file=sys.stderr)
            print(
                f"  python -c \"from transformers import AutoTokenizer; "
                f"AutoTokenizer.from_pretrained('laion/clap-htsat-unfused')"
                f".save_pretrained('{args.models_dir}')\"",
                file=sys.stderr,
            )
        return 1

    file_meta: Dict[str, Dict[str, object]] = {}
    for name in EXPECTED_FILES:
        p = args.models_dir / name
        file_meta[name] = {
            "path": p,
            "size": p.stat().st_size,
            "sha256": sha256_file(p),
        }

    model_card = build_model_card(args.repo_id, file_meta)

    if args.dry_run:
        print("=== Model card ===")
        print(model_card)
        print("=== Files ===")
        for name, meta in file_meta.items():
            print(f"  {name}  {fmt_size(int(meta['size']))}  sha256={meta['sha256']}")
        return 0

    upload(args.repo_id, args.token, file_meta, model_card)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
