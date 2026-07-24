"""LLM-teacher paraphrasing for data diversity (DeepSeek, OpenAI-compatible).

The template generators produce correct-by-construction (input, actions, gold
DSL) triples, but their *phrasing* is limited to what the template authors
wrote — which is why the model is brittle on novel wordings. This teacher keeps
the exact same gold DSL and asks an LLM to reword the natural-language side into
diverse, realistic phrasings.

Correctness is guaranteed by validation, not trust: each paraphrase is fed
through the same tag() -> reconstruct() -> render() round-trip as the templates,
and anything that doesn't reproduce the gold DSL is discarded. So a paraphrase
that drops/mangles a slot surface (a name, a value, a plugin) simply doesn't
make it into the dataset.

Guidance to raise yield: the prompt lists the exact slot surfaces (taken from
the original's own tags) that must survive verbatim.

Needs DEEPSEEK_API_KEY (env or repo-root .env). No SDK dependency — plain HTTP.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import threading
import time
import urllib.error
import urllib.request


def _load_cache(path):
    """Load the JSON cache, salvaging complete entries if the file was
    truncated by a crash mid-write (values are flat string lists)."""
    text = open(path, encoding="utf-8").read()
    try:
        return json.loads(text)
    except ValueError:
        out = {}
        for m in re.finditer(r'"([0-9a-f]{40})":\s*(\[[^\]]*\])', text):
            try:
                out[m.group(1)] = json.loads(m.group(2))
            except ValueError:
                pass
        return out

from dataset.tagging import _spans, roundtrip_ok, tag

API_URL = "https://api.deepseek.com/chat/completions"
DEFAULT_MODEL = "deepseek-chat"

_SYSTEM = (
    "You rewrite short digital-audio-workstation (DAW) commands the way a real "
    "music producer would speak or type them. You produce natural, varied "
    "phrasings — different verbs, word order, slang, and levels of politeness — "
    "but never change what the command does."
)


def load_api_key() -> str | None:
    if os.environ.get("DEEPSEEK_API_KEY"):
        return os.environ["DEEPSEEK_API_KEY"]
    # walk up looking for a .env with DEEPSEEK_API_KEY=...
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        env = os.path.join(d, ".env")
        if os.path.isfile(env):
            for line in open(env, encoding="utf-8"):
                line = line.strip()
                if line.startswith("DEEPSEEK_API_KEY="):
                    return line.split("=", 1)[1].strip().strip('"').strip("'")
        d = os.path.dirname(d)
    return None


def required_surfaces(nl: str, actions: list[dict]) -> list[str]:
    """The exact substrings that must survive a paraphrase — i.e. the slot
    surfaces the tagger found in the original text."""
    _, tokens, tags = tag(nl, actions)
    spans = _spans(tokens, tags)
    return [s for surfaces in spans.values() for s in surfaces]


def _prompt(nl: str, actions: list[dict], n: int, typo: bool = False) -> str:
    keep = required_surfaces(nl, actions)
    keep_line = (
        "Keep these exact words/values UNCHANGED and present in every rewrite: "
        + ", ".join(f'"{k}"' for k in keep) + ".\n"
        if keep else ""
    )
    typo_line = (
        "Make about half of the rewrites contain a realistic typo in a COMMON word "
        "(e.g. 'cretae', 'trasnpose', 'quantise', 'volme') — the kind a fast typist "
        "makes. NEVER misspell any of the exact words/values you must keep.\n"
        if typo else ""
    )
    return (
        f'Rewrite this DAW command in {n} different natural ways:\n"{nl}"\n\n'
        + keep_line + typo_line +
        "Vary verbs and sentence structure; sound like a real producer (casual is "
        "fine). Do NOT add, remove, or change any command or its target. "
        f'Return a JSON object: {{"rewrites": ["...", ...]}} with exactly {n} strings.'
    )


def _call(prompt: str, key: str, model: str, temperature: float, retries: int = 3) -> list[str]:
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": _SYSTEM},
                     {"role": "user", "content": prompt}],
        "temperature": temperature,
        "response_format": {"type": "json_object"},
    }).encode()
    req = urllib.request.Request(
        API_URL, data=body,
        headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                data = json.loads(resp.read())
            content = data["choices"][0]["message"]["content"]
            obj = json.loads(content)
            if isinstance(obj, list):                 # bare JSON array
                out = obj
            elif isinstance(obj, dict):               # {"rewrites": [...]} or similar
                out = obj.get("rewrites") or next(
                    (v for v in obj.values() if isinstance(v, list)), [])
            else:
                out = []
            return [s for s in out if isinstance(s, str)]
        # OSError: URLError/ConnectionReset/timeout; ValueError: JSON decode;
        # Key/Attr/TypeError: unexpected response shape. One bad call must never
        # kill a run — return nothing for it and move on.
        except (OSError, KeyError, ValueError, AttributeError, TypeError):
            if attempt == retries - 1:
                return []
            time.sleep(2.0 * (attempt + 1))
    return []


class Teacher:
    """Paraphrase with a persistent cache so re-runs are cheap and reproducible."""

    def __init__(self, key, model=DEFAULT_MODEL, temperature=1.3, cache_path=None):
        self.key, self.model, self.temperature = key, model, temperature
        self.cache_path = cache_path
        self.cache = {}
        self._lock = threading.Lock()
        if cache_path and os.path.isfile(cache_path):
            self.cache = _load_cache(cache_path)

    def _key(self, nl, n, typo):
        tag = "typo" if typo else "v1"   # separate namespace so typo/clean coexist
        return hashlib.sha1(f"{self.model}|{tag}|{n}|{nl}".encode()).hexdigest()

    def raw(self, nl, actions, n, typo=False):
        k = self._key(nl, n, typo)
        with self._lock:
            hit = self.cache.get(k)
        if hit is not None:
            return hit
        result = _call(_prompt(nl, actions, n, typo), self.key, self.model, self.temperature)
        with self._lock:
            self.cache[k] = result
        return result

    def save(self):
        if not self.cache_path:
            return
        with self._lock:                      # snapshot under lock, dump outside
            snapshot = dict(self.cache)
        os.makedirs(os.path.dirname(os.path.abspath(self.cache_path)), exist_ok=True)
        tmp = self.cache_path + ".tmp"        # atomic: write temp, then rename
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(snapshot, f)
        os.replace(tmp, self.cache_path)

    def paraphrases(self, nl, actions, output, n, typo=False):
        """Validated, deduped paraphrases (excluding the original)."""
        out, seen = [], {nl.lower().strip()}
        for cand in self.raw(nl, actions, n, typo):
            c = cand.strip()
            low = c.lower()
            if not c or low in seen:
                continue
            if roundtrip_ok({"input": c, "actions": actions, "output": output}):
                seen.add(low)
                out.append(c)
        return out
