"""Search the media DB by text (semantic) and/or scalar filters.

Prototype uses brute-force cosine similarity in NumPy. For O(N) corpora up to
~100k samples this is fine on a laptop. C++ runtime should swap in a vector
index (sqlite-vec, hnswlib) once we lock the schema and embed dim.
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


def search(
    conn: sqlite3.Connection,
    embedder: Embedder | None,
    text: str | None,
    filters: Filters,
    limit: int = 20,
) -> list[QueryResult]:
    where, params = _build_where(filters)

    if text is None or embedder is None:
        return _filter_only(conn, where, params, limit)

    qvec = embedder.embed_text([text])[0]  # (D,), L2-normalized

    sql = f"""
        SELECT f.id, f.path, f.kind, f.bpm, f.key_root, f.key_scale, f.duration_s,
               f.shape, f.family,
               e.vector_dim, e.vector_blob
        FROM media_file AS f
        JOIN media_embedding AS e
          ON e.file_id = f.id
         AND e.model_id = :model_id
         AND e.model_version = :model_version
        WHERE {where}
    """
    params = {**params, "model_id": embedder.model_id, "model_version": embedder.model_version}
    rows = conn.execute(sql, params).fetchall()
    if not rows:
        return []

    scored: list[tuple[float, sqlite3.Row]] = []
    for r in rows:
        v = dbmod.unpack_vector(bytes(r["vector_blob"]), int(r["vector_dim"]))
        scored.append((float(np.dot(qvec, v)), r))
    scored.sort(key=lambda x: -x[0])

    return [
        QueryResult(
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
        )
        for score, r in scored[:limit]
    ]


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
