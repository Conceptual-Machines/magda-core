"""SQLite connection + schema helpers.

Vectors are packed as raw float32 little-endian bytes so the same blob is read
identically by the C++ runtime via memcpy.
"""

from __future__ import annotations

import sqlite3
from importlib import resources
from pathlib import Path

import numpy as np

SCHEMA_VERSION = 7


def connect(db_path: Path | str) -> sqlite3.Connection:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def init(db_path: Path | str) -> sqlite3.Connection:
    conn = connect(db_path)
    schema = resources.files("media_db").joinpath("schema.sql").read_text()
    conn.executescript(schema)
    conn.commit()
    return conn


def pack_vector(vec: np.ndarray) -> bytes:
    """Pack a 1-D vector as raw float32 little-endian bytes."""
    arr = np.ascontiguousarray(vec, dtype="<f4")
    if arr.ndim != 1:
        raise ValueError(f"expected 1-D vector, got shape {arr.shape}")
    return arr.tobytes()


def unpack_vector(blob: bytes, dim: int) -> np.ndarray:
    """Inverse of pack_vector. C++ side does the equivalent memcpy."""
    arr = np.frombuffer(blob, dtype="<f4")
    if arr.size != dim:
        raise ValueError(f"blob size {arr.size} != dim {dim}")
    return arr.copy()
