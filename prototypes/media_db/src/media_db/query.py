"""Search the media DB by text (semantic) and/or scalar filters.

Hybrid ranking when a text query is present:
  combined = AUDIO_WEIGHT * audio_cosine + TEXT_WEIGHT * normalized_bm25

Filename + tag text matches (BM25 over media_fts) are essential for short
queries like 'kick': pure CLAP cosine is unreliable on single-token concepts
because percussion samples cluster, but a file literally named 'kick.wav'
is unambiguous. The audio side fills gaps for descriptive queries
('warm analog pad') where filenames don't help.

When the query produces no FTS hits and no audio embedder is loaded, the
function falls back to pure SQL filtering ordered by indexed_at.
"""

from __future__ import annotations

import sqlite3
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from . import db as dbmod
from .embeddings.base import Embedder


@dataclass
class QueryResult:
    file_id: int
    path: Path
    kind: str
    score: float
    bpm: float | None
    key_root: str | None
    key_scale: str | None
    duration_s: float | None
    shape: str | None = None
    family: str | None = None


@dataclass
class Filters:
    kind: str | None = None
    bpm_min: float | None = None
    bpm_max: float | None = None
    key_root: str | None = None
    key_scale: str | None = None
    format: str | None = None
    shape: str | None = None
    family: str | None = None
    tonal: bool | None = None


# Default hybrid weights. Text matches are weighted slightly higher because
# filename evidence is more reliable than CLAP cosine for short queries.
DEFAULT_AUDIO_WEIGHT = 0.45
DEFAULT_TEXT_WEIGHT = 0.55


def search(
    conn: sqlite3.Connection,
    embedder: Embedder | None,
    text: str | None,
    filters: Filters,
    limit: int = 20,
    audio_weight: float = DEFAULT_AUDIO_WEIGHT,
    text_weight: float = DEFAULT_TEXT_WEIGHT,
) -> list[QueryResult]:
    where, params = _build_where(filters)

    if text is None:
        return _filter_only(conn, where, params, limit)

    # Audio cosine for every embedded file matching the filters.
    audio_scores: dict[int, float] = {}
    if embedder is not None:
        qvec = embedder.embed_text([text])[0]
        sql = f"""
            SELECT f.id, e.vector_dim, e.vector_blob
            FROM media_file AS f
            JOIN media_embedding AS e
              ON e.file_id = f.id
             AND e.model_id = :model_id
             AND e.model_version = :model_version
            WHERE {where}
        """
        ap = {**params, "model_id": embedder.model_id, "model_version": embedder.model_version}
        for r in conn.execute(sql, ap):
            v = dbmod.unpack_vector(bytes(r["vector_blob"]), int(r["vector_dim"]))
            audio_scores[int(r["id"])] = float(np.dot(qvec, v))

    # FTS BM25 for files matching the filters AND containing query terms.
    text_scores = _fts_scores(conn, text, where, params)

    # Combine. Skip audio for files outside the filter set; skip text for
    # files with no FTS hit (text contribution = 0).
    candidate_ids = set(audio_scores) | set(text_scores)
    if not candidate_ids:
        return []

    # Normalize each component to [0, 1] across candidates so weights mean
    # what they say. Audio cosine is already roughly bounded; we just clamp.
    a_max = max(audio_scores.values(), default=1.0) or 1.0
    t_max = max(text_scores.values(), default=1.0) or 1.0

    combined: list[tuple[float, int]] = []
    for fid in candidate_ids:
        a = max(0.0, audio_scores.get(fid, 0.0)) / a_max if audio_scores else 0.0
        t = text_scores.get(fid, 0.0) / t_max if text_scores else 0.0
        score = audio_weight * a + text_weight * t
        combined.append((score, fid))
    combined.sort(key=lambda x: -x[0])

    return _hydrate(conn, [(s, fid) for s, fid in combined[:limit]])


def _fts_scores(
    conn: sqlite3.Connection, text: str, where: str, params: dict
) -> dict[int, float]:
    """Run an FTS5 MATCH and return {file_id: bm25_score} where higher is better.
    BM25 from FTS5 is 'lower=better' (negative-log-likelihood), so we negate."""
    fts_query = _build_fts_query(text)
    if not fts_query:
        return {}
    sql = f"""
        SELECT f.id, -bm25(media_fts) AS s
        FROM media_fts
        JOIN media_file AS f ON f.id = media_fts.rowid
        WHERE media_fts MATCH :fts AND {where}
    """
    try:
        rows = conn.execute(sql, {**params, "fts": fts_query}).fetchall()
    except sqlite3.OperationalError:
        # malformed query, e.g. user typed punctuation FTS5 didn't like
        return {}
    return {int(r["id"]): float(r["s"]) for r in rows}


def _build_fts_query(text: str) -> str:
    """Turn a free-text query into an FTS5 MATCH expression. We OR the terms
    with prefix matching so 'kick' also matches 'kicks' / 'kickdrum'."""
    import re
    tokens = [t for t in re.split(r"\s+", text.strip()) if t]
    safe = []
    for t in tokens:
        # keep only alnum + hyphen; FTS5 syntax forbids most punctuation here
        cleaned = re.sub(r"[^\w\-]", "", t)
        if len(cleaned) >= 2:
            safe.append(f'"{cleaned}"*')
    return " OR ".join(safe)


def _hydrate(
    conn: sqlite3.Connection, scored: list[tuple[float, int]]
) -> list[QueryResult]:
    if not scored:
        return []
    ids = [fid for _, fid in scored]
    placeholders = ",".join("?" * len(ids))
    rows = {
        int(r["id"]): r
        for r in conn.execute(
            f"SELECT id, path, kind, bpm, key_root, key_scale, duration_s, "
            f"shape, family FROM media_file WHERE id IN ({placeholders})",
            ids,
        )
    }
    out: list[QueryResult] = []
    for score, fid in scored:
        r = rows.get(fid)
        if r is None:
            continue
        out.append(QueryResult(
            file_id=int(r["id"]),
            path=Path(r["path"]),
            kind=r["kind"],
            score=score,
            bpm=r["bpm"],
            key_root=r["key_root"],
            key_scale=r["key_scale"],
            duration_s=r["duration_s"],
            shape=r["shape"],
            family=r["family"],
        ))
    return out


def _filter_only(
    conn: sqlite3.Connection, where: str, params: dict, limit: int
) -> list[QueryResult]:
    sql = f"""
        SELECT id, path, kind, bpm, key_root, key_scale, duration_s, shape, family
        FROM media_file
        WHERE {where}
        ORDER BY indexed_at DESC
        LIMIT :limit
    """
    rows = conn.execute(sql, {**params, "limit": limit}).fetchall()
    return [
        QueryResult(
            file_id=int(r["id"]),
            path=Path(r["path"]),
            kind=r["kind"],
            score=float("nan"),
            bpm=r["bpm"],
            key_root=r["key_root"],
            key_scale=r["key_scale"],
            duration_s=r["duration_s"],
            shape=r["shape"],
            family=r["family"],
        )
        for r in rows
    ]


def _build_where(f: Filters) -> tuple[str, dict]:
    clauses: list[str] = ["1=1"]
    params: dict = {}
    if f.kind is not None:
        clauses.append("kind = :kind")
        params["kind"] = f.kind
    if f.bpm_min is not None:
        clauses.append("bpm >= :bpm_min")
        params["bpm_min"] = f.bpm_min
    if f.bpm_max is not None:
        clauses.append("bpm <= :bpm_max")
        params["bpm_max"] = f.bpm_max
    if f.key_root is not None:
        clauses.append("key_root = :key_root")
        params["key_root"] = f.key_root
    if f.key_scale is not None:
        clauses.append("key_scale = :key_scale")
        params["key_scale"] = f.key_scale
    if f.format is not None:
        clauses.append("format = :format")
        params["format"] = f.format
    if f.shape is not None:
        clauses.append("shape = :shape")
        params["shape"] = f.shape
    if f.family is not None:
        clauses.append("family = :family")
        params["family"] = f.family
    if f.tonal is not None:
        clauses.append("tonal = :tonal")
        params["tonal"] = 1 if f.tonal else 0
    return " AND ".join(clauses), params
