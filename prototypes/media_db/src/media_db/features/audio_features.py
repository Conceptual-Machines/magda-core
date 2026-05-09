"""Deterministic audio features.

This module is **validation-only** for C++ portability — librosa is not shipped
with the C++ runtime. Each feature documents the algorithm so the C++ team can
reimplement using JUCE's FFT and standard DSP primitives.

Algorithms used:
  - duration / SR / channels: from soundfile read.
  - bpm:                       librosa.beat.beat_track (autocorrelation of onset
                               strength). C++ equivalent: onset envelope + ACF
                               peak picking.
  - key (root, scale):         chroma → Krumhansl-Schmuckler profile correlation.
                               C++ equivalent: chroma via constant-Q or filter
                               bank, dot product with two 12-vector profiles.
  - rms:                       mean of frame-wise RMS.
  - spectral_centroid:         mean of frame-wise centroid (sum(f*|S|)/sum(|S|)).
  - transient_density:         onsets per second from librosa.onset.onset_detect.
                               C++ equivalent: spectral flux peaks above adaptive
                               threshold, count / duration.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

PITCH_CLASSES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# Krumhansl-Schmuckler key profiles (Krumhansl 1990, normalized).
_MAJOR_PROFILE = np.array(
    [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
)
_MINOR_PROFILE = np.array(
    [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]
)


# Spectral flatness threshold for tonality. Flatness is in [0, 1]:
# tonal music ≈ 0.001-0.05, drums/noise ≈ 0.1-0.4. The chroma-profile correlation
# we use for key detection is biased upward and won't cleanly separate tonal from
# atonal (uniform chroma still scores ~0.8 against any rotated profile), so we use
# flatness as the dedicated tonality signal.
FLATNESS_THRESHOLD = 0.08


@dataclass
class AudioFeatures:
    duration_s: float
    sample_rate: int
    channels: int
    bpm: float | None
    key_root: str | None
    key_scale: str | None
    key_confidence: float | None     # chroma-profile correlation peak in [0, 1]
    rms: float
    spectral_centroid: float
    spectral_flatness: float          # [0, 1]; high = noisy/percussive, low = tonal
    transient_density: float


def extract(path: Path) -> AudioFeatures:
    import librosa
    import soundfile as sf

    info = sf.info(str(path))
    audio, sr = librosa.load(str(path), sr=None, mono=True)
    duration_s = len(audio) / sr if sr > 0 else 0.0

    bpm = _bpm(audio, sr)
    key_root, key_scale, key_conf = _key(audio, sr)
    rms = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))
    centroid = float(librosa.feature.spectral_centroid(y=audio, sr=sr).mean())
    flatness = float(librosa.feature.spectral_flatness(y=audio).mean())
    onsets = librosa.onset.onset_detect(y=audio, sr=sr, units="time")
    density = float(len(onsets) / duration_s) if duration_s > 0 else 0.0

    # Null out key_root/key_scale for non-tonal content — display would be wrong.
    if flatness >= FLATNESS_THRESHOLD:
        key_root, key_scale = None, None

    return AudioFeatures(
        duration_s=duration_s,
        sample_rate=int(info.samplerate),
        channels=int(info.channels),
        bpm=bpm,
        key_root=key_root,
        key_scale=key_scale,
        key_confidence=key_conf,
        rms=rms,
        spectral_centroid=centroid,
        spectral_flatness=flatness,
        transient_density=density,
    )


def _bpm(audio: np.ndarray, sr: int) -> float | None:
    import librosa
    try:
        tempo, _ = librosa.beat.beat_track(y=audio, sr=sr)
        t = float(np.atleast_1d(tempo)[0])
        return t if t > 0 else None
    except Exception:
        return None


def _key(audio: np.ndarray, sr: int) -> tuple[str | None, str | None, float | None]:
    """Krumhansl-Schmuckler: pick the (root, mode) maximizing correlation between
    the file's mean chroma and the rotated key profile. Returns the correlation
    peak in [0, 1] alongside the result; the caller decides whether the file is
    tonal enough for the key to be meaningful (atonal/percussive content scores
    very low even when *some* key wins by margin)."""
    import librosa
    try:
        chroma = librosa.feature.chroma_cqt(y=audio, sr=sr).mean(axis=1)
    except Exception:
        return None, None, None
    if not np.any(chroma):
        return None, None, 0.0

    chroma = chroma / (np.linalg.norm(chroma) + 1e-9)
    major = _MAJOR_PROFILE / np.linalg.norm(_MAJOR_PROFILE)
    minor = _MINOR_PROFILE / np.linalg.norm(_MINOR_PROFILE)

    best_score = -np.inf
    best_root = 0
    best_scale = "major"
    for i in range(12):
        rolled_major = np.roll(major, i)
        rolled_minor = np.roll(minor, i)
        s_maj = float(np.dot(chroma, rolled_major))
        s_min = float(np.dot(chroma, rolled_minor))
        if s_maj > best_score:
            best_score, best_root, best_scale = s_maj, i, "major"
        if s_min > best_score:
            best_score, best_root, best_scale = s_min, i, "minor"

    return PITCH_CLASSES[best_root], best_scale, max(0.0, best_score)
