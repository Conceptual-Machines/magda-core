"""Typer CLI: init, scan, query, stats, tags, export-onnx."""

from __future__ import annotations

from pathlib import Path
from typing import Optional

import typer
from rich.console import Console
from rich.table import Table

from . import db as dbmod
from . import query as qmod
from . import tags as tags_mod

app = typer.Typer(no_args_is_help=True, add_completion=False)
console = Console()

DEFAULT_MODEL = "laion/clap-htsat-unfused"


@app.command()
def init(db: Path = typer.Argument(..., help="Path to the SQLite DB to create.")) -> None:
    """Create or upgrade the media DB schema."""
    dbmod.init(db)
    console.print(f"[green]initialized[/green] {db}")


@app.command()
def scan(
    directory: Path = typer.Argument(..., exists=True, file_okay=False, resolve_path=True),
    db: Path = typer.Option(..., "--db", help="Path to the media DB."),
    model: str = typer.Option(DEFAULT_MODEL, "--model"),
    no_embed: bool = typer.Option(False, "--no-embed", help="Skip CLAP, only deterministic features."),
    tags_file: Optional[Path] = typer.Option(None, "--tags-file", help="YAML list of tag prompts."),
    tag_threshold: float = typer.Option(0.15, "--tag-threshold"),
    max_tags: int = typer.Option(8, "--max-tags"),
) -> None:
    """Walk DIRECTORY, index audio/preset/clip files into DB."""
    from .indexer import index_directory

    conn = dbmod.init(db)
    embedder = None
    if not no_embed:
        from .embeddings.clap import ClapEmbedder
        console.print(f"[cyan]loading[/cyan] {model} ...")
        embedder = ClapEmbedder(model_id=model)
        console.print(f"  device: {embedder.device}, dim: {embedder.vector_dim}")

    tag_list = tags_mod.load(tags_file) if embedder is not None else None
    res = index_directory(
        directory, conn, embedder, tag_list,
        tag_threshold=tag_threshold, max_tags=max_tags,
    )
    console.print(
        f"[green]done[/green] inserted={res.inserted} updated={res.updated} "
        f"skipped={res.skipped} failed={res.failed}"
    )


@app.command()
def query(
    text: Optional[str] = typer.Argument(None, help="Natural-language query."),
    db: Path = typer.Option(..., "--db"),
    model: str = typer.Option(DEFAULT_MODEL, "--model"),
    kind: Optional[str] = typer.Option(None, "--kind", help="audio|preset|clip"),
    bpm: Optional[str] = typer.Option(None, "--bpm", help="single (120) or range (120-130)"),
    key_root: Optional[str] = typer.Option(None, "--key-root"),
    key_scale: Optional[str] = typer.Option(None, "--key-scale"),
    format_: Optional[str] = typer.Option(None, "--format"),
    shape: Optional[str] = typer.Option(None, "--shape", help="one-shot|loop|sustained"),
    family: Optional[str] = typer.Option(None, "--family",
        help="drum|bass|lead|pad|keys|guitar|orchestral|vocal|fx"),
    tonal: Optional[bool] = typer.Option(None, "--tonal/--atonal", help="filter by tonality"),
    limit: int = typer.Option(20, "--limit"),
) -> None:
    """Search the DB. Provide TEXT for semantic search, filters narrow results."""
    conn = dbmod.connect(db)
    bpm_min, bpm_max = _parse_bpm(bpm)
    filters = qmod.Filters(
        kind=kind, bpm_min=bpm_min, bpm_max=bpm_max,
        key_root=key_root, key_scale=key_scale, format=format_,
        shape=shape, family=family, tonal=tonal,
    )

    embedder = None
    if text is not None:
        from .embeddings.clap import ClapEmbedder
        embedder = ClapEmbedder(model_id=model)

    results = qmod.search(conn, embedder, text, filters, limit=limit)
    _render(results)


@app.command()
def stats(db: Path = typer.Option(..., "--db")) -> None:
    """Show counts by kind, models indexed, embedding coverage."""
    conn = dbmod.connect(db)
    table = Table(title=str(db))
    table.add_column("kind")
    table.add_column("count", justify="right")
    for row in conn.execute("SELECT kind, COUNT(*) AS n FROM media_file GROUP BY kind"):
        table.add_row(row["kind"], str(row["n"]))
    console.print(table)

    models = conn.execute(
        "SELECT model_id, model_version, COUNT(*) AS n FROM media_embedding "
        "GROUP BY model_id, model_version"
    ).fetchall()
    if models:
        m = Table(title="embeddings")
        m.add_column("model_id"); m.add_column("version"); m.add_column("count", justify="right")
        for row in models:
            m.add_row(row["model_id"], row["model_version"][:12], str(row["n"]))
        console.print(m)


@app.command()
def tags(
    file_id: int = typer.Argument(...),
    db: Path = typer.Option(..., "--db"),
) -> None:
    """List tags for a single file."""
    conn = dbmod.connect(db)
    rows = conn.execute(
        "SELECT tag, confidence, source_model FROM media_tag "
        "WHERE file_id = ? ORDER BY confidence DESC",
        (file_id,),
    ).fetchall()
    if not rows:
        console.print("(no tags)")
        return
    t = Table()
    t.add_column("tag"); t.add_column("conf", justify="right"); t.add_column("model")
    for r in rows:
        t.add_row(r["tag"], f"{r['confidence']:.3f}", r["source_model"])
    console.print(t)


@app.command()
def serve(
    db: Path = typer.Option(..., "--db"),
    model: str = typer.Option(DEFAULT_MODEL, "--model"),
    host: str = typer.Option("127.0.0.1", "--host"),
    port: int = typer.Option(8765, "--port"),
) -> None:
    """Start a local web UI for browsing the DB. Browse to http://HOST:PORT/."""
    import uvicorn

    from .web.server import make_app

    console.print(f"[green]serving[/green] http://{host}:{port}/  (db={db})")
    uvicorn.run(make_app(db.resolve(), model), host=host, port=port, log_level="warning")


@app.command("export-onnx")
def export_onnx(
    out: Path = typer.Option(Path("models"), "--out"),
    model: str = typer.Option(DEFAULT_MODEL, "--model"),
) -> None:
    """Export CLAP audio + text encoders to ONNX, with a parity check vs PyTorch.
    This is the C++ portability gate."""
    from .embeddings.clap import ClapEmbedder
    from .embeddings.onnx_export import export

    console.print(f"[cyan]loading[/cyan] {model} ...")
    embedder = ClapEmbedder(model_id=model)
    console.print(f"[cyan]exporting[/cyan] to {out} ...")
    paths = export(embedder, out)
    console.print(f"[green]ok[/green] {paths}")


def _parse_bpm(arg: str | None) -> tuple[float | None, float | None]:
    if arg is None:
        return None, None
    if "-" in arg:
        a, b = arg.split("-", 1)
        return float(a), float(b)
    v = float(arg)
    return v, v


def _render(results: list[qmod.QueryResult]) -> None:
    if not results:
        console.print("(no results)")
        return
    t = Table()
    t.add_column("score", justify="right")
    t.add_column("family")
    t.add_column("shape")
    t.add_column("bpm", justify="right")
    t.add_column("key")
    t.add_column("dur", justify="right")
    t.add_column("path")
    for r in results:
        score = f"{r.score:.3f}" if r.score == r.score else "-"  # nan check
        bpm = f"{r.bpm:.1f}" if r.bpm else "-"
        key = f"{r.key_root or '-'} {r.key_scale or ''}".strip()
        dur = f"{r.duration_s:.1f}s" if r.duration_s else "-"
        t.add_row(score, r.family or "-", r.shape or "-", bpm, key, dur, str(r.path))
    console.print(t)


if __name__ == "__main__":
    app()
