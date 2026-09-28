#!/usr/bin/env python3
"""Upload the MAGDA beat-tracker ONNX model to HuggingFace.

Pushes beat_this.onnx to the configured HF repo and writes a model card
carrying provenance, size, SHA-256 and the MIT notices. Same shape as
scripts/upload_sample_tagger_to_hf.py, and the SHA-256 it prints is what
BeatModelDownloader's manifest has to match (issue #2674).

Usage:
    HF_TOKEN=hf_... python scripts/upload_beat_tracker_to_hf.py \\
        --model ~/Library/MAGDA/MediaDB/models/beat_this.onnx \\
        --repo-id ConceptualMachines/magda-beat-tracker

Dependencies:
    pip install huggingface_hub
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


MODEL_FILENAME = "beat_this.onnx"


def sha256_file(path: Path) -> str:
    """Stream-hash so we don't load 83 MB into memory."""
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


def build_model_card(size: int, digest: str) -> str:
    # Flat, not dedented: the YAML frontmatter --- markers must sit at column 0
    # or HuggingFace will not parse the license and tags.
    return f"""\
---
license: mit
tags:
- audio
- beat-tracking
- tempo-estimation
- onnx
library_name: onnxruntime
---

# MAGDA Beat Tracker

ONNX export of [Beat This!](https://github.com/CPJKU/beat_this) (CPJKU), packaged
for the [MAGDA DAW](https://github.com/Conceptual-Machines/magda-core)'s media
library (issue #2674).

## What's in this repo

| File | Size | SHA-256 |
|------|------|---------|
| `{MODEL_FILENAME}` | {fmt_size(size)} | `{digest}` |

A transformer over log-mel frames. It takes a whole file at once and returns a
beat and a downbeat logit per 20 ms frame; MAGDA reads a tempo off the spacing
of the beats and a bar length off the spacing of the downbeats.

## Input contract

The mel front end has to match the one the model was trained with, or the
frames mean nothing to it:

| | |
|---|---|
| sample rate | 22050 Hz, mono |
| FFT / window | 1024, Hann |
| hop | 441 (50 frames a second) |
| mel bands | 128, Slaney scale, 30 Hz - 11 kHz, unnormalised triangles |
| spectrum | magnitude, divided by sqrt(window length) |
| compression | `log1p(1000 * x)` |
| tensor | `input_spectrogram`, `[1, frames, 128]` |
| outputs | `beat`, `downbeat`, both `[1, frames]` logits |

## How MAGDA uses it

The media database's third BPM tier. A file's tempo is read from its filename
first and its ACID chunk second; when neither says anything, this measures it.
An answer is kept only when the beats it found are steady, because a wrong
tempo seeds a clip and stretches it while a missing one leaves a field blank.

Without the model MAGDA falls back to an autocorrelation over the spectral flux
envelope it computes anyway -- which is right far less often, and says so.

## Provenance

- Model and weights: [CPJKU/beat_this](https://github.com/CPJKU/beat_this), MIT.
- ONNX conversion: [mosynthkey/beat_this_cpp](https://github.com/mosynthkey/beat_this_cpp),
  MIT, converted from the upstream PyTorch checkpoint with opset 14.

## License

MIT, as upstream. Note the upstream caveat: some of the material the model was
trained on is copyrighted or under limited Creative Commons terms, and it is on
the user to decide whether that bears on their use.
"""


def upload(repo_id: str, token: str | None, model_path: Path, model_card: str) -> None:
    # Imported lazily so --dry-run works without huggingface_hub installed.
    from huggingface_hub import HfApi, create_repo

    api = HfApi(token=token)
    create_repo(repo_id, exist_ok=True, repo_type="model", token=token)

    api.upload_file(
        path_or_fileobj=model_card.encode("utf-8"),
        path_in_repo="README.md",
        repo_id=repo_id,
        repo_type="model",
        commit_message="model card",
    )

    print(f"Uploading {MODEL_FILENAME} ({fmt_size(model_path.stat().st_size)})...", flush=True)
    api.upload_file(
        path_or_fileobj=str(model_path),
        path_in_repo=MODEL_FILENAME,
        repo_id=repo_id,
        repo_type="model",
        commit_message=f"add {MODEL_FILENAME}",
    )

    print(f"\nDone. Repo: https://huggingface.co/{repo_id}")
    print("Direct download URL (this is what the in-app downloader wants):")
    print(f"  https://huggingface.co/{repo_id}/resolve/main/{MODEL_FILENAME}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "--model",
        type=Path,
        required=True,
        help=f"Path to {MODEL_FILENAME}",
    )
    parser.add_argument(
        "--repo-id",
        default="ConceptualMachines/magda-beat-tracker",
        help="HuggingFace repo id (default: %(default)s)",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="HF write-token. Falls back to HF_TOKEN env var / cached `hf auth login`.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the model card and the manifest entry, don't push.",
    )
    args = parser.parse_args()

    if not args.model.is_file():
        print(f"Missing: {args.model.resolve()}", file=sys.stderr)
        return 1

    size = args.model.stat().st_size
    digest = sha256_file(args.model)
    model_card = build_model_card(size, digest)

    print(f"{MODEL_FILENAME}  {fmt_size(size)}  {digest}\n")
    print("BeatModelDownloader manifest entry:\n")
    print(f'    "{MODEL_FILENAME}",')
    print(f'    "https://huggingface.co/{args.repo_id}/resolve/main/{MODEL_FILENAME}",')
    print(f'    "{digest}",')
    print(f"    {size},\n")

    if args.dry_run:
        print(model_card)
        return 0

    upload(args.repo_id, args.token, args.model, model_card)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
