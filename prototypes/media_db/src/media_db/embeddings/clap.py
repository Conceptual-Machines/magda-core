"""LAION-CLAP embedder via HuggingFace transformers.

Audio path:
  1. Resample to 48 kHz mono float32.
  2. The ClapProcessor builds the model's mel spectrogram input.
  3. ClapModel.get_audio_features() yields a 512-dim vector.
  4. L2-normalize.

For files longer than the model window (~10s), we chunk and average the
unit-normalized chunk embeddings, then renormalize. This matches what the
C++ runtime should do post-ONNX-export.
"""

from __future__ import annotations

import numpy as np
import torch

DEFAULT_MODEL = "laion/clap-htsat-unfused"
TARGET_SR = 48_000
CHUNK_SECONDS = 10  # CLAP nominal window
VECTOR_DIM = 512


class ClapEmbedder:
    model_id: str
    model_version: str
    vector_dim: int = VECTOR_DIM
    sample_rate: int = TARGET_SR

    def __init__(self, model_id: str = DEFAULT_MODEL, device: str | None = None) -> None:
        from transformers import ClapModel, ClapProcessor  # lazy: heavy import

        self.model_id = model_id
        self.device = device or _pick_device()
        self.processor = ClapProcessor.from_pretrained(model_id)
        self.model = ClapModel.from_pretrained(model_id).to(self.device).eval()
        # store the model's revision/commit if available
        cfg = getattr(self.model, "config", None)
        self.model_version = getattr(cfg, "_commit_hash", None) or "unknown"

    @torch.inference_mode()
    def embed_audio(self, audio: np.ndarray, sr: int) -> np.ndarray:
        if audio.ndim != 1:
            audio = audio.mean(axis=0) if audio.ndim == 2 else audio.reshape(-1)
        audio = audio.astype(np.float32, copy=False)

        if sr != TARGET_SR:
            audio = _resample(audio, sr, TARGET_SR)

        chunks = _chunk(audio, TARGET_SR * CHUNK_SECONDS)
        inputs = self.processor(audio=chunks, sampling_rate=TARGET_SR, return_tensors="pt")
        inputs = {k: v.to(self.device) for k, v in inputs.items()}
        # transformers 5.x: pooler_output is the projected + L2-normalized embedding
        feats = self.model.get_audio_features(**inputs).pooler_output  # (N, 512)
        pooled = feats.mean(dim=0)
        pooled = torch.nn.functional.normalize(pooled, dim=-1)
        return pooled.detach().cpu().numpy().astype(np.float32)

    @torch.inference_mode()
    def embed_text(self, texts: list[str]) -> np.ndarray:
        inputs = self.processor(text=texts, return_tensors="pt", padding=True)
        inputs = {k: v.to(self.device) for k, v in inputs.items()}
        feats = self.model.get_text_features(**inputs).pooler_output  # (N, 512)
        return feats.detach().cpu().numpy().astype(np.float32)


def _pick_device() -> str:
    if torch.cuda.is_available():
        return "cuda"
    if torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def _chunk(audio: np.ndarray, chunk_len: int) -> list[np.ndarray]:
    if audio.size <= chunk_len:
        return [audio]
    n = (audio.size + chunk_len - 1) // chunk_len
    out: list[np.ndarray] = []
    for i in range(n):
        start = i * chunk_len
        end = min(start + chunk_len, audio.size)
        seg = audio[start:end]
        if seg.size < chunk_len:
            pad = np.zeros(chunk_len - seg.size, dtype=np.float32)
            seg = np.concatenate([seg, pad])
        out.append(seg)
    return out


def _resample(audio: np.ndarray, sr_in: int, sr_out: int) -> np.ndarray:
    import librosa  # lazy
    return librosa.resample(audio, orig_sr=sr_in, target_sr=sr_out).astype(np.float32)
