"""Sanity checks on the librosa feature extractor against synthetic signals."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

soundfile = pytest.importorskip("soundfile")

from media_db.features.audio_features import extract


def _write_wav(path: Path, audio: np.ndarray, sr: int) -> None:
    soundfile.write(str(path), audio, sr, subtype="FLOAT")


def test_features_on_sine(tmp_path: Path) -> None:
    sr = 44_100
    f0 = 440.0
    t = np.linspace(0, 2.0, int(sr * 2.0), endpoint=False)
    audio = (0.5 * np.sin(2 * np.pi * f0 * t)).astype(np.float32)
    p = tmp_path / "sine.wav"
    _write_wav(p, audio, sr)

    f = extract(p)
    assert f.sample_rate == sr
    assert f.channels == 1
    assert f.duration_s == pytest.approx(2.0, abs=0.05)
    # RMS of a 0.5-amplitude sine ≈ 0.5/sqrt(2)
    assert f.rms == pytest.approx(0.5 / np.sqrt(2), abs=0.02)
    # Spectral centroid of a 440Hz pure tone should be near 440.
    assert 350 < f.spectral_centroid < 550


def test_features_on_silence(tmp_path: Path) -> None:
    sr = 22_050
    audio = np.zeros(sr, dtype=np.float32)
    p = tmp_path / "silence.wav"
    _write_wav(p, audio, sr)

    f = extract(p)
    assert f.rms == 0.0
    assert f.transient_density == 0.0
    # key detection on silence is undefined; just assert it doesn't crash.
    assert f.key_root is None or isinstance(f.key_root, str)
