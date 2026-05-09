from __future__ import annotations

from pathlib import Path

from media_db import scan


def _touch(p: Path, content: bytes = b"x") -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(content)


def test_classify_audio(tmp_path: Path) -> None:
    p = tmp_path / "kick.wav"
    _touch(p)
    sf = scan.classify(p)
    assert sf is not None
    assert sf.kind == "audio"
    assert sf.format == "wav"


def test_classify_preset(tmp_path: Path) -> None:
    p = tmp_path / "lead.vstpreset"
    _touch(p)
    sf = scan.classify(p)
    assert sf is not None
    assert sf.kind == "preset"


def test_classify_clip(tmp_path: Path) -> None:
    p = tmp_path / "loop.mid"
    _touch(p)
    sf = scan.classify(p)
    assert sf is not None
    assert sf.kind == "clip"


def test_classify_unknown_returns_none(tmp_path: Path) -> None:
    p = tmp_path / "readme.txt"
    _touch(p)
    assert scan.classify(p) is None


def test_walk_recurses_and_filters(tmp_path: Path) -> None:
    _touch(tmp_path / "a/b/kick.wav")
    _touch(tmp_path / "a/snare.aiff")
    _touch(tmp_path / "a/notes.txt")
    _touch(tmp_path / "loop.mid")
    out = sorted(s.path.name for s in scan.walk(tmp_path))
    assert out == ["kick.wav", "loop.mid", "snare.aiff"]
