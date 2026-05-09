"""Indexing pipeline: scan → features → embedding → tag scoring → DB write.

Skip strategy: a file with the same path, mtime_ns, size_bytes, and
content_hash as an existing row is left alone. content_hash is a fast xxh64
of the first 1 MiB; collisions are tolerable for a sample-browser cache.
"""

from __future__ import annotations

import re
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
import xxhash
from rich.progress import Progress

from . import db as dbmod
from . import derive, scan, tags
from .embeddings.base import Embedder
from .features.audio_features import AudioFeatures, extract as extract_features

HASH_PREFIX_BYTES = 1 << 20  # 1 MiB

# Tokenize a path into searchable terms. Underscores, slashes, dashes, dots
# are separators (sample packs love these). FTS5's tokenizer further splits
# on punctuation so case + diacritics fall through.
_PATH_TOKEN_SPLIT = re.compile(r"[_/\-.\s]+")


def _path_to_search_text(path: str) -> str:
    """Build a space-separated keyword string from the file path. Drops the
    extension and dedupes tokens to keep the FTS index tight."""
    p = Path(path)
    raw = " ".join(p.parts) + " " + p.stem
    tokens = [t for t in _PATH_TOKEN_SPLIT.split(raw) if t]
    seen: set[str] = set()
    out: list[str] = []
    for t in tokens:
        lo = t.lower()
        if lo not in seen:
            seen.add(lo)
            out.append(t)
    return " ".join(out)


def _tags_to_search_text(top_tags: list[tuple[str, float]]) -> str:
    """Tags are stored as 'a kick drum' style prompts; index just the keyword
    payload so users can grep by 'kick' or 'pad' etc."""
    parts: list[str] = []
    for tag, _ in top_tags:
        parts.append(tag.replace("the sound of ", ""))
    return " ".join(parts)


@dataclass
class IndexResult:
    inserted: int = 0
    updated: int = 0
    skipped: int = 0
    failed: int = 0


def index_directory(
    root: Path,
    conn: sqlite3.Connection,
    embedder: Embedder | None,
    tag_list: list[str] | None,
    tag_threshold: float = 0.15,
    max_tags: int = 8,
) -> IndexResult:
    files = list(scan.walk(root))
    result = IndexResult()

    text_embeddings = None
    if embedder is not None and tag_list:
        text_embeddings = embedder.embed_text(tags.prompts(tag_list))  # (T, D)

    with Progress() as progress:
        task = progress.add_task("indexing", total=len(files))
        for sf_ in files:
            try:
                state = _index_one(
                    sf_, conn, embedder, tag_list, text_embeddings,
                    tag_threshold, max_tags,
                )
                if state == "inserted":
                    result.inserted += 1
                elif state == "updated":
                    result.updated += 1
                else:
                    result.skipped += 1
            except Exception as e:
                progress.console.log(f"[red]failed[/red] {sf_.path}: {e}")
                result.failed += 1
            progress.advance(task)
    conn.commit()
    return result


def _index_one(
    f: scan.ScannedFile,
    conn: sqlite3.Connection,
    embedder: Embedder | None,
    tag_list: list[str] | None,
    text_embeddings: np.ndarray | None,
    tag_threshold: float,
    max_tags: int,
) -> str:
    existing = conn.execute(
        "SELECT id, mtime_ns, size_bytes, content_hash FROM media_file WHERE path = ?",
        (str(f.path),),
    ).fetchone()

    content_hash = _xxh64_prefix(f.path)
    if existing is not None:
        same = (
            existing["mtime_ns"] == f.mtime_ns
            and existing["size_bytes"] == f.size_bytes
            and bytes(existing["content_hash"] or b"") == content_hash
        )
        if same:
            return "skipped"

    feats: AudioFeatures | None = None
    if f.kind == "audio":
        feats = extract_features(f.path)

    top_tags: list[tuple[str, float]] = []
    vec = None
    if embedder is not None and f.kind == "audio":
        audio, sr = sf.read(str(f.path), dtype="float32", always_2d=False)
        if audio.ndim == 2:
            audio = audio.mean(axis=1)
        vec = embedder.embed_audio(audio, sr)
        if tag_list and text_embeddings is not None:
            scores = text_embeddings @ vec  # cosine, both L2-normalized
            top_tags = _top_k(scores, tag_list, tag_threshold, max_tags)

    path_tags_ = derive.path_tags(f.path) if f.kind == "audio" else []
    derived = _derive_categoricals(f.kind, feats, top_tags, f.path)

    # Apply derivation policies that adjust stored feature values:
    # - one-shots have no meaningful tempo (single transient or short burst)
    # - drums and fx have no meaningful key (a ride cymbal IS tonal but
    #   "key=G#" on a hi-hat or impact is producer-useless noise)
    # - filename-encoded key trumps chroma analysis when present (after the
    #   family check, so a path-tagged kick still has its key nulled)
    if feats is not None and f.kind == "audio":
        if derived["shape"] == "one-shot":
            feats.bpm = None
        path_key = derive.parse_key_from_path(f.path)
        if path_key is not None:
            feats.key_root, feats.key_scale = path_key
        if derived["family"] in ("drum", "fx"):
            feats.key_root, feats.key_scale = None, None

    file_id = _upsert_file(conn, f, content_hash, feats, derived)

    if vec is not None and embedder is not None:
        _upsert_embedding(conn, file_id, embedder, vec)
        if top_tags:
            _replace_tags(conn, file_id, top_tags, source_model=embedder.model_id)

    if path_tags_:
        _replace_tags(conn, file_id, path_tags_, source_model="path")

    # FTS sees CLAP tags + path tags so search matches either source.
    _upsert_fts(conn, file_id, str(f.path), top_tags + path_tags_)
    return "updated" if existing is not None else "inserted"


def _upsert_fts(
    conn: sqlite3.Connection, file_id: int, path: str,
    top_tags: list[tuple[str, float]],
) -> None:
    conn.execute("DELETE FROM media_fts WHERE rowid = ?", (file_id,))
    conn.execute(
        "INSERT INTO media_fts (rowid, path_text, tag_text) VALUES (?, ?, ?)",
        (file_id, _path_to_search_text(path), _tags_to_search_text(top_tags)),
    )


def _derive_categoricals(
    kind: str, feats: AudioFeatures | None, top_tags: list[tuple[str, float]],
    path: Path,
) -> dict[str, object]:
    """Compute shape, family, tonal columns. Non-audio kinds get nulls/unknowns."""
    if kind != "audio" or feats is None:
        return {"shape": None, "family": None, "tonal": None}
    return {
        "shape": derive.shape(feats),
        "family": derive.family(top_tags, path),
        "tonal": int(derive.tonal(feats)),
    }


def _upsert_file(
    conn: sqlite3.Connection,
    f: scan.ScannedFile,
    content_hash: bytes,
    feats: AudioFeatures | None,
    derived: dict[str, object],
) -> int:
    now = int(time.time())
    audio_cols: dict[str, object] = {}
    if feats is not None:
        audio_cols = {
            "duration_s": feats.duration_s,
            "sample_rate": feats.sample_rate,
            "channels": feats.channels,
            "bpm": feats.bpm,
            "key_root": feats.key_root,
            "key_scale": feats.key_scale,
            "key_confidence": feats.key_confidence,
            "rms": feats.rms,
            "spectral_centroid": feats.spectral_centroid,
            "spectral_flatness": feats.spectral_flatness,
            "transient_density": feats.transient_density,
        }

    base_cols = {
        "path": str(f.path),
        "kind": f.kind,
        "format": f.format,
        "size_bytes": f.size_bytes,
        "mtime_ns": f.mtime_ns,
        "content_hash": content_hash,
        "indexed_at": now,
    }
    cols = {**base_cols, **audio_cols, **derived}
    placeholders = ", ".join(f":{k}" for k in cols)
    column_list = ", ".join(cols)
    update_list = ", ".join(f"{k}=excluded.{k}" for k in cols if k != "path")

    cur = conn.execute(
        f"INSERT INTO media_file ({column_list}) VALUES ({placeholders}) "
        f"ON CONFLICT(path) DO UPDATE SET {update_list} RETURNING id",
        cols,
    )
    row = cur.fetchone()
    return int(row["id"])


def _upsert_embedding(
    conn: sqlite3.Connection, file_id: int, embedder: Embedder, vec: np.ndarray
) -> None:
    blob = dbmod.pack_vector(vec)
    conn.execute(
        "INSERT OR REPLACE INTO media_embedding "
        "(file_id, model_id, model_version, vector_dim, vector_blob) "
        "VALUES (?, ?, ?, ?, ?)",
        (file_id, embedder.model_id, embedder.model_version, embedder.vector_dim, blob),
    )


def _replace_tags(
    conn: sqlite3.Connection, file_id: int, scored: list[tuple[str, float]],
    source_model: str,
) -> None:
    conn.execute(
        "DELETE FROM media_tag WHERE file_id = ? AND source_model = ?",
        (file_id, source_model),
    )
    conn.executemany(
        "INSERT INTO media_tag (file_id, tag, confidence, source_model) VALUES (?, ?, ?, ?)",
        [(file_id, tag, float(conf), source_model) for tag, conf in scored],
    )


def _top_k(
    scores: np.ndarray, tags_: list[str], threshold: float, k: int
) -> list[tuple[str, float]]:
    idx = np.argsort(-scores)
    out: list[tuple[str, float]] = []
    for i in idx[:k]:
        s = float(scores[i])
        if s < threshold:
            break
        out.append((tags_[int(i)], s))
    return out


def _xxh64_prefix(path: Path) -> bytes:
    h = xxhash.xxh64()
    with path.open("rb") as f:
        h.update(f.read(HASH_PREFIX_BYTES))
    return h.digest()  # 8 bytes
