"""LLM-teacher paraphrasing for phrasing diversity (DeepSeek, OpenAI-compatible).

The template generators produce correct-by-construction (text, label) pairs, but
their *phrasing* is limited to what the template author wrote. That is the whole
remaining error mode: held-out core failures are requests whose intent is
concrete but whose wording the closed vocabulary has never seen ("half time
feel", "boom bap kit"). Synonym banks took core accuracy from 59.3% to 81.5%;
paraphrases are how it goes further, because they introduce words a template
author would not think of.

Unlike the command model's teacher (dataset/teacher.py) there is no slot
round-trip to validate against — a router paraphrase only has to keep its
*label*. But that is exactly the failure to guard: a rewrite that turns
"suggest jazz chords" (MUSIC) into "add jazz chords to a new track" (BOTH) is a
mislabeled row, which is worse than no row at all. So this runs two passes:

  1. paraphrase — N rewrites of a source request, same language, same intent
  2. verify     — hand the rewrites back with the label definitions and keep
                  only those the teacher independently labels the same

Pass 2 roughly doubles the call count and is worth it: it is the only automatic
check standing between a drifting paraphrase and a poisoned training set.

Needs DEEPSEEK_API_KEY (env or repo-root .env). No SDK dependency — plain HTTP.

    python -m router.teacher --per-class 40 --n 6
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import random
import sys
import threading
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from router.data import load_rows, open_rows  # noqa: E402
from router.labels import LABELS  # noqa: E402
from router.text import tokenize  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)

API_URL = "https://api.deepseek.com/chat/completions"
# NB: "deepseek-chat" is no longer accepted by the API (it now serves
# deepseek-v4-pro / deepseek-v4-flash). The command POC's dataset/teacher.py
# still defaults to the old name and will 400 until it is updated too.
DEFAULT_MODEL = "deepseek-v4-flash"

LANG_NAME = {"en": "English", "ja": "Japanese", "ru": "Russian", "zh": "Chinese"}

# The label contract, stated once and reused by both passes so the paraphraser
# and the verifier are working from the same definitions.
LABEL_SPEC = """COMMAND — change project structure: create/delete/rename tracks, add or remove FX, mute/solo, volume/pan/colour, clips, quantize/transpose, tempo, racks.
MUSIC — generate musical content only, with no project change: chord progressions, melodies, harmony, reharmonisation.
BOTH — generate musical content AND place it (e.g. make a track and write a progression on it).
AUTOMATION — automation curves or automation clips: LFO shapes, sweeps, fades, clearing or resizing automation.
DRUM — drum patterns and groove edits: beats, fills, swing, humanising, making parts busier or sparser.
MIXING — mix analysis and mixing advice: muddiness, masking, levels, headroom, stereo width, clipping.
SESSION — clip launch, scenes, performance: launching/stopping clips or scenes, record arming, capturing a take."""

_SYSTEM = (
    "You rewrite short digital-audio-workstation (DAW) requests the way a real "
    "music producer would speak or type them. You produce natural, varied "
    "phrasings — different verbs, word order, slang, and levels of politeness — "
    "but never change what the user is asking the DAW to do."
)

_VERIFY_SYSTEM = (
    "You classify digital-audio-workstation (DAW) requests into exactly one "
    "agent category. Answer only with category names."
)


def load_api_key() -> str | None:
    if os.environ.get("DEEPSEEK_API_KEY"):
        return os.environ["DEEPSEEK_API_KEY"]
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        env = os.path.join(d, ".env")
        if os.path.isfile(env):
            with open(env, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("DEEPSEEK_API_KEY="):
                        return line.split("=", 1)[1].strip().strip('"').strip("'")
        d = os.path.dirname(d)
    return None


def _paraphrase_prompt(text: str, label: str, lang: str, n: int) -> str:
    return (
        f'Rewrite this DAW request in {n} different natural ways:\n"{text}"\n\n'
        f"Write every rewrite in {LANG_NAME[lang]}, the same language as the original.\n"
        f"The request's category is {label}. Every rewrite must still be {label}:\n"
        f"{LABEL_SPEC}\n\n"
        "Critically: do NOT add or remove any action. If the original only asks for "
        "musical content, do not make the rewrite also create a track. If the "
        "original changes the project, do not turn it into a question. Keep any "
        "@plugin references, numbers and units exactly as they appear.\n"
        "Vary verbs, word order and register; sound like a real producer (casual and "
        "abbreviated is good — that is what users actually type). "
        f'Return JSON: {{"rewrites": ["...", ...]}} with exactly {n} strings.'
    )


def _verify_prompt(texts: list[str]) -> str:
    listing = "\n".join(f"{i + 1}. {t}" for i, t in enumerate(texts))
    return (
        "Classify each DAW request into exactly one category.\n\n"
        f"{LABEL_SPEC}\n\n"
        f"Requests:\n{listing}\n\n"
        f'Return JSON: {{"labels": ["CATEGORY", ...]}} with exactly {len(texts)} '
        "entries, in the same order."
    )


_reported = set()
_report_lock = threading.Lock()


def _report_once(msg: str):
    """Print each distinct API failure once. Without this a wrong model name
    looks identical to 'the teacher just didn't return anything'."""
    with _report_lock:
        if msg in _reported:
            return
        _reported.add(msg)
    print(f"  ! teacher API error — {msg}")


def _call(system: str, prompt: str, key: str, model: str, temperature: float,
          retries: int = 3):
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": system},
                     {"role": "user", "content": prompt}],
        "temperature": temperature,
        "response_format": {"type": "json_object"},
    }).encode()
    req = urllib.request.Request(
        API_URL, data=body,
        headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=90) as resp:
                data = json.loads(resp.read())
            obj = json.loads(data["choices"][0]["message"]["content"])
            if isinstance(obj, list):
                out = obj
            elif isinstance(obj, dict):
                out = next((v for v in obj.values() if isinstance(v, list)), [])
            else:
                out = []
            return [s for s in out if isinstance(s, str)]
        except urllib.error.HTTPError as e:
            # 4xx is a bug (bad model name, bad key), not weather. Retrying it
            # just burns time and hides the cause — surface it once and stop.
            if 400 <= e.code < 500:
                _report_once(f"HTTP {e.code}: {e.read()[:200].decode('utf-8', 'replace')}")
                return []
            if attempt == retries - 1:
                return []
            time.sleep(2.0 * (attempt + 1))
        # One bad call must never kill a run — give back nothing and move on.
        except (OSError, KeyError, ValueError, AttributeError, TypeError) as e:
            if attempt == retries - 1:
                _report_once(f"{type(e).__name__}: {e}")
                return []
            time.sleep(2.0 * (attempt + 1))
    return []


class Teacher:
    """Two-pass paraphrasing with a persistent cache so re-runs are free."""

    def __init__(self, key, model=DEFAULT_MODEL, temperature=1.3, cache_path=None):
        self.key, self.model, self.temperature = key, model, temperature
        self.cache_path = cache_path
        self.cache = {}
        self._lock = threading.Lock()
        self.calls = 0
        if cache_path and os.path.isfile(cache_path):
            with open(cache_path, encoding="utf-8") as f:
                try:
                    self.cache = json.load(f)
                except ValueError:
                    self.cache = {}

    def _cached(self, kind: str, payload: str, make):
        k = hashlib.sha1(f"{self.model}|{kind}|{payload}".encode()).hexdigest()
        with self._lock:
            hit = self.cache.get(k)
        if hit is not None:
            return hit
        result = make()
        with self._lock:
            self.cache[k] = result
            self.calls += 1
        return result

    def rewrites(self, text, label, lang, n):
        return self._cached(
            f"para|{n}|{lang}|{label}", text,
            lambda: _call(_SYSTEM, _paraphrase_prompt(text, label, lang, n),
                          self.key, self.model, self.temperature))

    def labels(self, texts):
        return self._cached(
            "verify", "\n".join(texts),
            # temperature 0: verification should be deterministic, not creative
            lambda: _call(_VERIFY_SYSTEM, _verify_prompt(texts),
                          self.key, self.model, 0.0))

    def save(self):
        if not self.cache_path:
            return
        with self._lock:
            snapshot = dict(self.cache)
        os.makedirs(os.path.dirname(os.path.abspath(self.cache_path)), exist_ok=True)
        tmp = self.cache_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(snapshot, f)
        os.replace(tmp, self.cache_path)


def harvest(teacher, row, n, excluded):
    """Paraphrase one row, then keep only the rewrites the teacher re-labels
    identically. Returns (kept_rows, n_proposed, n_agreed)."""
    text, label, lang = row["input"], row["label"], row["lang"]
    cands, seen = [], {text.lower().strip()}
    for c in teacher.rewrites(text, label, lang, n):
        c = c.strip()
        low = c.lower()
        if not c or low in seen or c in excluded:
            continue
        if not tokenize(c):  # must survive the router's own tokenizer
            continue
        seen.add(low)
        cands.append(c)
    if not cands:
        return [], 0, 0

    verdicts = teacher.labels(cands)
    kept = []
    for c, v in zip(cands, verdicts):
        if isinstance(v, str) and v.strip().upper() == label:
            kept.append({"lang": lang, "input": c, "label": label, "src": "teacher"})
    return kept, len(cands), len(kept)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default=os.path.join(POC, "data", "train.jsonl.gz"))
    ap.add_argument("--out", default=os.path.join(POC, "data", "teacher.jsonl.gz"))
    ap.add_argument("--cache", default=os.path.join(POC, "data", ".teacher_cache.json"))
    ap.add_argument("--per-class", type=int, default=40,
                    help="source rows sampled per (language, label)")
    ap.add_argument("--n", type=int, default=6, help="rewrites requested per row")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    key = load_api_key()
    if not key:
        print("DEEPSEEK_API_KEY not found (env or repo-root .env)")
        return 1

    rows = load_rows(args.train)
    test_path = os.path.join(POC, "eval", "testset.jsonl")
    excluded = set()
    if os.path.exists(test_path):
        excluded = {r["input"] for r in load_rows(test_path)}

    # Sample evenly across (lang, label) so the teacher does not simply amplify
    # COMMAND, which is already the largest class by a wide margin.
    rng = random.Random(args.seed)
    buckets = {}
    for r in rows:
        buckets.setdefault((r["lang"], r["label"]), []).append(r)
    sources = []
    for kk in sorted(buckets):
        pool = buckets[kk]
        rng.shuffle(pool)
        sources.extend(pool[:args.per_class])
    print(f"{len(sources)} source rows across {len(buckets)} (lang, label) buckets; "
          f"requesting {args.n} rewrites each")

    teacher = Teacher(key, model=args.model, cache_path=args.cache)
    out, proposed, agreed = [], 0, 0
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(harvest, teacher, r, args.n, excluded): r for r in sources}
        for fut in concurrent.futures.as_completed(futures):
            kept, p, a = fut.result()
            out.extend(kept)
            proposed += p
            agreed += a
            done += 1
            if done % 50 == 0:
                print(f"  {done}/{len(sources)} rows | {len(out)} kept | "
                      f"agreement {agreed / max(proposed, 1):.0%}")
                teacher.save()
    teacher.save()

    # Dedupe against the existing corpus as well as within the harvest.
    have = {r["input"] for r in rows} | excluded
    final, seen = [], set()
    for r in out:
        if r["input"] in have or r["input"] in seen:
            continue
        seen.add(r["input"])
        final.append(r)

    with open_rows(args.out, "wt") as f:
        for r in final:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")

    from collections import Counter
    print(f"\nproposed {proposed}, teacher agreed on {agreed} ({agreed/max(proposed,1):.0%}), "
          f"{len(final)} new after dedupe")
    print(f"  by label: {dict(Counter(r['label'] for r in final))}")
    print(f"  by lang:  {dict(Counter(r['lang'] for r in final))}")
    print(f"  -> {args.out} ({os.path.getsize(args.out)/1048576:.2f} MB), "
          f"{teacher.calls} API calls this run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
