from __future__ import annotations

from pathlib import Path

import numpy as np

from media_db import db as dbmod


def test_init_creates_schema(tmp_path: Path) -> None:
    conn = dbmod.init(tmp_path / "media.db")
    tables = {
        r["name"]
        for r in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"
        ).fetchall()
    }
    assert {"media_file", "media_embedding", "media_tag"} <= tables
    assert conn.execute("PRAGMA user_version").fetchone()[0] == dbmod.SCHEMA_VERSION


def test_pack_unpack_roundtrip() -> None:
    rng = np.random.default_rng(0)
    v = rng.standard_normal(512).astype(np.float32)
    blob = dbmod.pack_vector(v)
    assert len(blob) == 512 * 4
    out = dbmod.unpack_vector(blob, 512)
    np.testing.assert_array_equal(v, out)


def test_pack_is_little_endian_float32() -> None:
    """Layout the C++ side reads back via memcpy: float32 LE, contiguous."""
    v = np.array([1.0, -1.0, 0.5], dtype=np.float32)
    blob = dbmod.pack_vector(v)
    expected = v.astype("<f4").tobytes()
    assert blob == expected


def test_kind_check_constraint(tmp_path: Path) -> None:
    conn = dbmod.init(tmp_path / "media.db")
    import sqlite3
    try:
        conn.execute(
            "INSERT INTO media_file (path, kind, format, size_bytes, mtime_ns, indexed_at) "
            "VALUES ('x', 'bogus', 'wav', 0, 0, 0)"
        )
    except sqlite3.IntegrityError:
        return
    raise AssertionError("expected IntegrityError on bogus kind")


def test_embedding_fk_cascade(tmp_path: Path) -> None:
    conn = dbmod.init(tmp_path / "media.db")
    conn.execute(
        "INSERT INTO media_file (path, kind, format, size_bytes, mtime_ns, indexed_at) "
        "VALUES ('x', 'audio', 'wav', 0, 0, 0)"
    )
    file_id = conn.execute("SELECT id FROM media_file WHERE path='x'").fetchone()["id"]
    blob = dbmod.pack_vector(np.zeros(4, dtype=np.float32))
    conn.execute(
        "INSERT INTO media_embedding VALUES (?, 'm', 'v', 4, ?)",
        (file_id, blob),
    )
    conn.execute("DELETE FROM media_file WHERE id = ?", (file_id,))
    n = conn.execute("SELECT COUNT(*) AS n FROM media_embedding").fetchone()["n"]
    assert n == 0
