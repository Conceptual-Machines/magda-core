"""Export CLAP audio + text encoders to ONNX for the C++ runtime.

The audio encoder takes a fixed-length 48 kHz mono waveform tensor
(shape [batch, samples]) and returns a 512-dim vector.

We export the audio encoder as `clap_audio.onnx` and the text encoder as
`clap_text.onnx`. The C++ runtime loads both via ONNX Runtime; preprocessing
(resampling, chunking, mel-spectrogram if not fused into the graph) must match
the Python wrapper bit-for-bit.

We also include a parity check: run the same audio through the PyTorch model
and the ONNX session, and assert max-abs-diff < 1e-3. If this fails, the C++
embedding will diverge from the prototype's DB and search results will be wrong.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import torch

from .clap import TARGET_SR, ClapEmbedder

OPSET = 17


def export(embedder: ClapEmbedder, out_dir: Path) -> dict[str, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    audio_path = out_dir / "clap_audio.onnx"
    text_path = out_dir / "clap_text.onnx"

    # Trace on CPU. MPS lacks float64 which F.normalize's eps clamp uses.
    # Inference can use MPS afterwards; export is one-shot.
    original_device = embedder.device
    embedder.model.to("cpu")
    embedder.device = "cpu"
    try:
        _export_audio(embedder, audio_path)
        _export_text(embedder, text_path)
        _check_parity(embedder, audio_path)
    finally:
        embedder.model.to(original_device)
        embedder.device = original_device
    return {"audio": audio_path, "text": text_path}


def _export_audio(emb: ClapEmbedder, path: Path) -> None:
    model = emb.model
    device = emb.device
    chunk_samples = TARGET_SR * 10
    dummy = torch.zeros(1, chunk_samples, device=device)

    # The processor normally builds a mel spectrogram; for prototype simplicity
    # we export from the post-processor input ('input_features'). The C++ side
    # therefore needs to compute the same mel spectrogram. If we want a
    # waveform-in graph instead, fuse the feature_extractor as a torch module
    # and trace through it — left as a follow-up once we lock the model.
    inputs = emb.processor(audio=dummy.cpu().numpy()[0], sampling_rate=TARGET_SR,
                           return_tensors="pt")
    input_features = inputs["input_features"].to(device)

    class AudioWrapper(torch.nn.Module):
        def __init__(self, m: torch.nn.Module) -> None:
            super().__init__()
            self.m = m

        def forward(self, input_features: torch.Tensor) -> torch.Tensor:
            # transformers 5.x: pooler_output is the projected + L2-normalized embedding.
            return self.m.get_audio_features(input_features=input_features).pooler_output

    wrapped = AudioWrapper(model).eval()
    torch.onnx.export(
        wrapped,
        (input_features,),
        str(path),
        input_names=["input_features"],
        output_names=["embedding"],
        dynamic_axes={"input_features": {0: "batch"}, "embedding": {0: "batch"}},
        opset_version=OPSET,
        dynamo=False,  # legacy TorchScript exporter; new dynamo path chokes on CLAP internals
    )


def _export_text(emb: ClapEmbedder, path: Path) -> None:
    model = emb.model
    device = emb.device
    inputs = emb.processor(text=["placeholder text"], return_tensors="pt", padding=True)
    input_ids = inputs["input_ids"].to(device)
    attention_mask = inputs["attention_mask"].to(device)

    class TextWrapper(torch.nn.Module):
        def __init__(self, m: torch.nn.Module) -> None:
            super().__init__()
            self.m = m

        def forward(self, input_ids: torch.Tensor, attention_mask: torch.Tensor) -> torch.Tensor:
            return self.m.get_text_features(
                input_ids=input_ids, attention_mask=attention_mask
            ).pooler_output

    wrapped = TextWrapper(model).eval()
    torch.onnx.export(
        wrapped,
        (input_ids, attention_mask),
        str(path),
        input_names=["input_ids", "attention_mask"],
        output_names=["embedding"],
        dynamic_axes={
            "input_ids": {0: "batch", 1: "seq"},
            "attention_mask": {0: "batch", 1: "seq"},
            "embedding": {0: "batch"},
        },
        opset_version=OPSET,
        dynamo=False,
    )


def _check_parity(emb: ClapEmbedder, audio_path: Path, tol: float = 1e-3) -> None:
    """Run the same input through PyTorch and ONNX Runtime; assert max abs diff < tol."""
    try:
        import onnxruntime as ort  # type: ignore[import-not-found]
    except ImportError as e:
        raise RuntimeError(
            "onnxruntime is required for ONNX parity check. "
            "`uv pip install onnxruntime` and re-run."
        ) from e

    rng = np.random.default_rng(0)
    wave = rng.standard_normal(TARGET_SR * 10).astype(np.float32) * 0.1
    inputs = emb.processor(audio=wave, sampling_rate=TARGET_SR, return_tensors="pt")
    input_features = inputs["input_features"].to(emb.device)

    with torch.inference_mode():
        torch_out = emb.model.get_audio_features(input_features=input_features).pooler_output
        torch_np = torch_out.detach().cpu().numpy()

    sess = ort.InferenceSession(str(audio_path), providers=["CPUExecutionProvider"])
    onnx_out = sess.run(None, {"input_features": input_features.detach().cpu().numpy()})[0]

    diff = float(np.max(np.abs(torch_np - onnx_out)))
    if diff > tol:
        raise RuntimeError(f"ONNX/PyTorch parity check failed: max abs diff {diff:.6f} > {tol}")
