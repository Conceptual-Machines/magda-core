"""Embedder protocol. Implementations must produce L2-normalized float32 vectors.

The exact same shape and normalization is what the C++ runtime expects from its
ONNX inference path, so cosine similarity reduces to a dot product on both sides.
"""

from __future__ import annotations

from typing import Protocol, runtime_checkable

import numpy as np


@runtime_checkable
class Embedder(Protocol):
    model_id: str           # e.g. 'laion/larger_clap_music'
    model_version: str      # commit / weights hash for reproducibility
    vector_dim: int
    sample_rate: int        # required input SR; caller resamples to match

    def embed_audio(self, audio: np.ndarray, sr: int) -> np.ndarray:
        """Return a 1-D float32 vector of length self.vector_dim, L2-normalized.
        `audio` is mono float32 PCM. If sr != self.sample_rate, the implementation
        resamples internally."""

    def embed_text(self, texts: list[str]) -> np.ndarray:
        """Return an (N, vector_dim) float32 matrix, L2-normalized per row."""
