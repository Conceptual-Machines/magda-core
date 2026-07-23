"""Score a parser/model against a test set. Reports the POC success metrics.

Metrics (per the spec, adapted to a DSL target instead of raw JSON):
  valid_syntax   -- output parses under the shipping grammar (grammar.lark)
  valid_command  -- parses AND passes semantic checks
  exact_match    -- normalized output == normalized gold DSL
  latency_ms     -- mean wall-clock per example

All are reported overall AND per language, so a per-language split decision can
be made from data rather than assumption.
"""
from __future__ import annotations

import json
import time
from collections import defaultdict

from magda_dsl import dsl, validate


def load_testset(path: str):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def evaluate(rows, parser_fn):
    """parser_fn: str -> DSL str. Returns (overall, per_lang, records)."""
    buckets = defaultdict(lambda: {
        "n": 0, "valid_syntax": 0, "valid_command": 0, "exact_match": 0, "latency_ms": 0.0,
    })
    records = []

    for row in rows:
        lang = row.get("lang", "en")
        t0 = time.perf_counter()
        pred = parser_fn(row["input"])
        dt = (time.perf_counter() - t0) * 1000.0

        v = validate.validate(pred)
        exact = dsl.normalize(pred) == dsl.normalize(row["output"])

        for key in ("__all__", lang):
            b = buckets[key]
            b["n"] += 1
            b["valid_syntax"] += int(v.parse_ok)
            b["valid_command"] += int(v.valid)
            b["exact_match"] += int(exact)
            b["latency_ms"] += dt

        records.append({
            "lang": lang, "input": row["input"], "gold": row["output"],
            "pred": pred, "valid": v.valid, "exact": exact, "errors": v.errors,
        })

    def finalize(b):
        n = max(b["n"], 1)
        return {
            "n": b["n"],
            "valid_syntax": b["valid_syntax"] / n,
            "valid_command": b["valid_command"] / n,
            "exact_match": b["exact_match"] / n,
            "latency_ms": b["latency_ms"] / n,
        }

    overall = finalize(buckets["__all__"])
    per_lang = {k: finalize(v) for k, v in buckets.items() if k != "__all__"}
    return overall, per_lang, records


def format_report(overall, per_lang, title="results"):
    def line(name, m):
        return (f"  {name:<8} n={m['n']:<4} "
                f"syntax={m['valid_syntax']:6.1%}  "
                f"command={m['valid_command']:6.1%}  "
                f"exact={m['exact_match']:6.1%}  "
                f"lat={m['latency_ms']:5.2f}ms")

    out = [f"== {title} =="]
    out.append(line("OVERALL", overall))
    for lang in sorted(per_lang):
        out.append(line(lang, per_lang[lang]))
    return "\n".join(out)
