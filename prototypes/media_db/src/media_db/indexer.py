"""Indexing pipeline: scan → features → embedding → tag scoring → DB write.

Skip strategy: a file with the same path, mtime_ns, size_bytes, and
content_hash as an existing row is left alone. content_hash is a fast xxh64
of the first 1 MiB; collisions are tolerable for a sample-browser cache.
"""

from __future__ import annotations

import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
import xxhash
from rich.progress import Progress

from . import db as dbmod
from . import scan, tags
from .embeddings.base import Embedder
from .features.audio_features import AudioFeatures, extract as extract_features

HASH_PREFIX_BYTES = 1 << 20  # 1 MiB


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

    file_id = _upsert_file(conn, f, content_hash, feats)

    if embedder is not None and f.kind == "audio":
        audio, sr = sf.read(str(f.path), dtype="float32", always_2d=False)
        if audio.ndim == 2:
            audio = audio.mean(axis=1)
        vec = embedder.embed_audio(audio, sr)
        _upsert_embedding(conn, file_id, embedder, vec)

        if tag_list and text_embeddings is not None:
            scores = text_embeddings @ vec  # cosine, both L2-normalized
            top = _top_k(scores, tag_list, tag_threshold, max_tags)
            _replace_tags(conn, file_id, top, embedder.model_id)

    return "updated" if existing is not None else "inserted"


def _upsert_file(
    conn: sqlite3.Connection,
    f: scan.ScannedFile,
    content_hash: bytes,
    feats: AudioFeatures | None,
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
            "rms": feats.rms,
            "spectral_centroid": feats.spectral_centroid,
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
    cols = {**base_cols, **audio_cols}
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
    conn: sqlite3.Connection, file_id: int, scored: list[tuple[str, float]], model_id: str
) -> None:
    conn.execute(
        "DELETE FROM media_tag WHERE file_id = ? AND source_model = ?",
        (file_id, model_id),
    )
    conn.executemany(
        "INSERT INTO media_tag (file_id, tag, confidence, source_model) VALUES (?, ?, ?, ?)",
        [(file_id, tag, float(conf), model_id) for tag, conf in scored],
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
