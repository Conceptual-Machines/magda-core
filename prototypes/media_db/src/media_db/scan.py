"""Filesystem walking + file-kind classification.

Kinds match the schema CHECK constraint: 'audio', 'preset', 'clip'.
The C++ scanner will use the same extension table.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

AUDIO_EXTS = {".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg", ".m4a"}
PRESET_EXTS = {".vstpreset", ".aupreset", ".fxp", ".fxb", ".mps"}
CLIP_EXTS = {".mid", ".midi"}

_KIND_BY_EXT: dict[str, str] = {}
for ext in AUDIO_EXTS:
    _KIND_BY_EXT[ext] = "audio"
for ext in PRESET_EXTS:
    _KIND_BY_EXT[ext] = "preset"
for ext in CLIP_EXTS:
    _KIND_BY_EXT[ext] = "clip"


@dataclass(frozen=True)
class ScannedFile:
    path: Path
    kind: str
    format: str
    size_bytes: int
    mtime_ns: int


def classify(path: Path) -> ScannedFile | None:
    ext = path.suffix.lower()
    kind = _KIND_BY_EXT.get(ext)
    if kind is None:
        return None
    try:
        st = path.stat()
    except OSError:
        return None
    return ScannedFile(
        path=path,
        kind=kind,
        format=ext.lstrip("."),
        size_bytes=st.st_size,
        mtime_ns=st.st_mtime_ns,
    )


def walk(root: Path) -> Iterator[ScannedFile]:
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        sf = classify(p)
        if sf is not None:
            yield sf
