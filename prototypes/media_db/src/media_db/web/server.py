"""FastAPI app for browsing the media DB in a browser.

Endpoints:
    GET /              -> static index.html
    GET /search        -> JSON list of QueryResult-like dicts
    GET /tags/{id}     -> JSON list of {tag, confidence}
    GET /audio/{id}    -> raw audio file (for <audio> playback)

Run via `media-db serve --db data/media.db`. Single-process uvicorn,
bound to localhost. Not meant for production — purely a prototype tool.
"""

from __future__ import annotations

import sqlite3
from importlib import resources
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse, HTMLResponse

from .. import db as dbmod
from .. import query as qmod


def make_app(db_path: Path, model: str) -> FastAPI:
    app = FastAPI(title="Media DB Browser")

    # Lazy embedder so we don't pay model-load cost unless someone actually
    # types a query. Filter-only browsing works without it.
    state: dict[str, object] = {"embedder": None}

    def conn() -> sqlite3.Connection:
        return dbmod.connect(db_path)

    def embedder():
        if state["embedder"] is None:
            from ..embeddings.clap import ClapEmbedder
            state["embedder"] = ClapEmbedder(model_id=model)
        return state["embedder"]

    @app.get("/", response_class=HTMLResponse)
    def index() -> str:
        return resources.files("media_db.web").joinpath("static/index.html").read_text()

    @app.get("/search")
    def search(
        q: Optional[str] = None,
        kind: Optional[str] = None,
        family: Optional[str] = None,
        shape: Optional[str] = None,
        tonal: Optional[bool] = None,
        bpm_min: Optional[float] = Query(None, alias="bpmMin"),
        bpm_max: Optional[float] = Query(None, alias="bpmMax"),
        limit: int = 30,
    ) -> dict:
        filters = qmod.Filters(
            kind=kind, family=family, shape=shape, tonal=tonal,
            bpm_min=bpm_min, bpm_max=bpm_max,
        )
        emb = embedder() if q else None
        c = conn()
        results = qmod.search(c, emb, q if q else None, filters, limit=limit)
        # Batch-fetch top tags for all returned ids in one query
        ids = [r.file_id for r in results]
        tags_by_id: dict[int, list[dict]] = {i: [] for i in ids}
        if ids:
            placeholders = ",".join(["?"] * len(ids))
            # Path tags first (deterministic), then CLAP tags by confidence
            for row in c.execute(
                f"SELECT file_id, tag, confidence, source_model FROM media_tag "
                f"WHERE file_id IN ({placeholders}) "
                f"ORDER BY file_id, "
                f"  CASE WHEN source_model = 'path' THEN 0 ELSE 1 END, "
                f"  confidence DESC",
                ids,
            ):
                lst = tags_by_id[row["file_id"]]
                if len(lst) < 6:
                    lst.append({
                        "tag": row["tag"].replace("the sound of ", ""),
                        "confidence": float(row["confidence"]),
                        "source": "path" if row["source_model"] == "path" else "clap",
                    })
        return {
            "results": [
                {
                    "id": r.file_id,
                    "name": Path(r.path).name,
                    "path": str(r.path),
                    "score": None if r.score != r.score else r.score,
                    "kind": r.kind,
                    "family": r.family,
                    "shape": r.shape,
                    "bpm": r.bpm,
                    "key": _format_key(r.key_root, r.key_scale),
                    "duration_s": r.duration_s,
                    "tags": tags_by_id[r.file_id],
                }
                for r in results
            ]
        }

    @app.get("/tags/{file_id}")
    def tags(file_id: int) -> dict:
        rows = conn().execute(
            "SELECT tag, confidence FROM media_tag WHERE file_id=? "
            "ORDER BY confidence DESC LIMIT 8",
            (file_id,),
        ).fetchall()
        return {
            "tags": [
                {
                    "tag": r["tag"].replace("the sound of ", ""),
                    "confidence": float(r["confidence"]),
                }
                for r in rows
            ]
        }

    @app.get("/audio/{file_id}")
    def audio(file_id: int) -> FileResponse:
        row = conn().execute(
            "SELECT path FROM media_file WHERE id=?", (file_id,)
        ).fetchone()
        if row is None:
            raise HTTPException(404, "file not found")
        path = Path(row["path"]).resolve()
        # Follow symlinks (the prototype symlinks Splice files into data/sample_set)
        if not path.exists():
            raise HTTPException(404, "file missing on disk")
        return FileResponse(path)

    @app.get("/stats")
    def stats() -> dict:
        c = conn()
        kinds = {r["kind"]: r["n"] for r in c.execute(
            "SELECT kind, COUNT(*) n FROM media_file GROUP BY kind")}
        families = {r["family"] or "unknown": r["n"] for r in c.execute(
            "SELECT family, COUNT(*) n FROM media_file GROUP BY family")}
        shapes = {r["shape"] or "unknown": r["n"] for r in c.execute(
            "SELECT shape, COUNT(*) n FROM media_file GROUP BY shape")}
        return {"kinds": kinds, "families": families, "shapes": shapes}

    return app


def _format_key(root: str | None, scale: str | None) -> str | None:
    if not root:
        return None
    return f"{root} {scale or ''}".strip()
