"""Renders the committed OpenRPC document into the Remote API operations page."""

import json
from pathlib import Path

PAGE = "reference/remote-api-operations.md"
DOCUMENT = "reference/magda-openrpc.json"
MARKER = "<!-- openrpc:operations -->"


def _cell(text):
    return str(text).replace("|", "\\|").replace("\n", " ")


def _type(schema):
    if "enum" in schema:
        values = schema["enum"]
        return " \\| ".join(json.dumps(v) for v in values) if len(values) <= 6 else "enum"
    if "const" in schema:
        return json.dumps(schema["const"])
    kind = schema.get("type")
    if isinstance(kind, list):
        return " \\| ".join(kind)
    if kind == "array" and isinstance(schema.get("items"), dict):
        return f"array of {_type(schema['items'])}"
    if kind:
        return kind
    alternatives = schema.get("oneOf", schema.get("anyOf"))
    if alternatives and all("const" in a for a in alternatives):
        return " \\| ".join(json.dumps(a["const"]) for a in alternatives)
    if alternatives:
        return "one of several shapes"
    return "any"


def _groups(method):
    """The x-params-schema alternatives, as "Exactly one of: `a` + `b`, `c`"."""
    schema = method.get("x-params-schema", {})
    notes = []
    for keyword, lead in (("oneOf", "Exactly one of"), ("anyOf", "At least one of")):
        alternatives = schema.get(keyword)
        if not alternatives:
            continue
        if all(set(a) == {"required"} for a in alternatives):
            groups = (" + ".join(f"`{n}`" for n in a["required"]) for a in alternatives)
            notes.append(f"*{lead}:* " + ", ".join(groups))
        else:
            notes.append("*Allowed combinations: see `x-params-schema`.*")
    return notes


def _params(method, required):
    chosen = [p for p in method["params"] if p.get("required", False) == required]
    lines = [f"`{p['name']}` {_type(p['schema'])}" for p in chosen]
    if not required:
        lines += _groups(method)
    return "<br>".join(lines) or "—"


def _render(document):
    groups = {}
    for method in document["methods"]:
        groups.setdefault(method["name"].split(".")[0], []).append(method)

    lines = []
    for group, methods in groups.items():
        lines += [f"## `{group}`", "",
                  "| Method | Summary | Scope | Required params | Optional params |",
                  "|--------|---------|-------|-----------------|-----------------|"]
        for method in methods:
            summary = method.get("summary", "")
            if method.get("x-transport-scoped"):
                summary += " *(transport-scoped)*"
            lines.append(f"| `{method['name']}` | {_cell(summary)} "
                         f"| `{method['x-required-scope']}` "
                         f"| {_params(method, True)} "
                         f"| {_params(method, False)} |")
        lines.append("")

    lines += ["## Errors", "",
              "| JSON-RPC code | `error.data.code` | Meaning |",
              "|---------------|-------------------|---------|"]
    for error in document["components"]["errors"].values():
        name = error.get("data", {}).get("code")
        lines.append(f"| `{error['code']}` | {f'`{name}`' if name else '—'} "
                     f"| {_cell(error['message'])} |")
    return "\n".join(lines) + "\n"


def on_page_markdown(markdown, page, config, files):
    if page.file.src_uri != PAGE:
        return markdown
    document = json.loads(Path(config["docs_dir"], DOCUMENT).read_text(encoding="utf-8"))
    return markdown.replace(MARKER, _render(document))
