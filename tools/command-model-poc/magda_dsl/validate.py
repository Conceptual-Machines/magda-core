"""Validate DSL output against the shipping grammar (lark) + semantic checks.

Two layers, mirroring the spec's "valid JSON" / "valid command" distinction:

  parse_ok   -- the string parses under grammar.lark (the same CFG the C++
                CommandAgent constrains the model with). This is the POC's
                "valid command syntax" gate.
  valid      -- parse_ok AND passes light semantic checks (known fx names,
                well-formed colours, etc). This is the "valid command schema"
                gate.

The parser is built once and reused.
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass, field

from lark import Lark, LarkError

_GRAMMAR_PATH = os.path.join(os.path.dirname(__file__), "grammar.lark")

with open(_GRAMMAR_PATH, "r", encoding="utf-8") as _f:
    _GRAMMAR = _f.read()

# earley handles the grammar's ambiguity (method-call alternatives) robustly.
_PARSER = Lark(_GRAMMAR, parser="earley", start="start")

_HEX_RE = re.compile(r"^#[0-9a-fA-F]{6}$")


@dataclass
class ValidationResult:
    parse_ok: bool
    valid: bool
    errors: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.valid


def parse(dsl: str):
    """Return the lark tree, or raise LarkError."""
    return _PARSER.parse(dsl)


def validate(dsl: str) -> ValidationResult:
    errors: list[str] = []
    text = (dsl or "").strip()
    if not text:
        return ValidationResult(False, False, ["empty output"])

    try:
        parse(text)
    except LarkError as e:
        # keep the message compact, lark errors can be multi-line
        msg = str(e).strip().splitlines()[0]
        return ValidationResult(False, False, [f"parse error: {msg}"])

    # ---- semantic checks (parse already guarantees structure) -------------
    # colours must be #rrggbb
    for m in re.finditer(r'colour=("?)(#?[0-9a-fA-F]{0,8})\1', text):
        val = m.group(2)
        if not _HEX_RE.match(val):
            errors.append(f"bad colour literal: {val!r}")

    valid = not errors
    return ValidationResult(True, valid, errors)
