from __future__ import annotations

import pytest

from media_db import derive

CASES = [
    # (filename, expected_root, expected_scale)
    ("kick_C.wav", "C", None),
    ("synth_Cm.wav", "C", "minor"),
    ("synth_Cmin.wav", "C", "minor"),
    ("synth_Cminor.wav", "C", "minor"),
    ("synth_Cmaj.wav", "C", "major"),
    ("synth_Cmajor.wav", "C", "major"),
    ("synth_C#.wav", "C#", None),
    ("synth_F#m.wav", "F#", "minor"),
    ("synth_Bb.wav", "A#", None),                 # Bb -> A# normalization
    ("synth_Bbmin.wav", "A#", "minor"),
    ("synth_Db_loop.wav", "C#", None),            # Db -> C#
    # split-token form: "C_minor"
    ("pad_C_minor.wav", "C", "minor"),
    ("pad_F#_major.wav", "F#", "major"),
    # last match wins when multiple keys appear
    ("C_to_F.wav", "F", None),
    # nested in the middle of a name
    ("MTVR2_120bpm_Cm.wav", "C", "minor"),
    # last token but bare letter
    ("kick_punchy_F.wav", "F", None),
]

NEGATIVE_CASES = [
    "Modern_Trap_Vocals.wav",                     # 'M' 'T' 'V' all uppercase but not key
    "Animal_Sounds.wav",                          # no key tokens
    "guitar_strum.wav",                           # 'g' is lowercase, not matched
    "kick_punchy.wav",                            # no key marker at all
]


@pytest.mark.parametrize("filename,root,scale", CASES)
def test_parse_key_positive(filename: str, root: str, scale: str | None) -> None:
    got = derive.parse_key_from_path(filename)
    assert got == (root, scale), f"{filename}: expected {(root, scale)}, got {got}"


@pytest.mark.parametrize("filename", NEGATIVE_CASES)
def test_parse_key_negative(filename: str) -> None:
    assert derive.parse_key_from_path(filename) is None, filename
