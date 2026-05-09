from __future__ import annotations

from media_db import derive
from media_db.features.audio_features import AudioFeatures


def _f(**overrides) -> AudioFeatures:
    base = dict(
        duration_s=4.0, sample_rate=44100, channels=1,
        bpm=120.0, key_root="C", key_scale="major", key_confidence=0.7,
        rms=0.1, spectral_centroid=2000.0, spectral_flatness=0.02,
        transient_density=2.0,
    )
    base.update(overrides)
    return AudioFeatures(**base)


def test_shape_oneshot_for_short_audio() -> None:
    assert derive.shape(_f(duration_s=0.4, transient_density=2.5)) == "one-shot"


def test_shape_loop_for_dense_long() -> None:
    assert derive.shape(_f(duration_s=8.0, transient_density=4.0)) == "loop"


def test_shape_sustained_for_sparse_long() -> None:
    assert derive.shape(_f(duration_s=20.0, transient_density=0.1)) == "sustained"


def test_shape_unknown_for_zero_duration() -> None:
    assert derive.shape(_f(duration_s=0.0)) == "unknown"


def test_family_picks_top_instrument_tag() -> None:
    top = [("a synth pad", 0.42), ("a warm sound", 0.35), ("strings", 0.30)]
    assert derive.family(top) == "pad"


def test_family_skips_texture_tags() -> None:
    """A 'warm sound' tag must not produce family='texture' if a real instrument
    is also above the floor — the structured family is for instrument filters."""
    top = [("a warm sound", 0.50), ("a synth pad", 0.30)]
    assert derive.family(top) == "pad"


def test_family_unknown_when_below_floor() -> None:
    top = [("a kick drum", 0.10)]  # below FAMILY_TAG_FLOOR (0.20)
    assert derive.family(top) == "unknown"


def test_family_unknown_for_empty_tags() -> None:
    assert derive.family([]) == "unknown"


def test_tonal_true_for_low_flatness() -> None:
    assert derive.tonal(_f(spectral_flatness=0.01)) is True


def test_tonal_false_for_high_flatness() -> None:
    """Drums and noise have high flatness — should be flagged atonal."""
    assert derive.tonal(_f(spectral_flatness=0.30)) is False
