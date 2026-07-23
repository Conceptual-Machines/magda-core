"""Parsers: raw corpus files -> per-song {key?, chords:[(root_pc, quality)]}.

Each returns an iterator of dicts:
  {"id": str, "tonic_pc": int|None, "mode": str|None,
   "chords": [(root_pc:int, quality_class:str), ...]}

tonic_pc/mode are filled when the corpus annotates a key; otherwise left None
and inferred later (build.py -> roman.infer_key). These parsers are real but
inert until the corresponding corpus is downloaded into data/raw/ (see
sources.py); they are exercised by smoke.py via hand-fed strings.
"""
from __future__ import annotations

import glob
import os
import re

from chords.roman import note_to_pc
from chords.vocab import normalize_quality

_HARTE = re.compile(r"^([A-G][b#]?):?(.*)$")


def parse_harte_chord(sym: str):
    """'C:maj7', 'A:min', 'G:7', 'D:maj/5' -> (root_pc, quality_class) or None."""
    sym = sym.strip()
    if sym in ("N", "N.C.", "&pause", "*", ""):
        return None
    m = _HARTE.match(sym)
    if not m:
        return None
    root_pc = note_to_pc(m.group(1))
    qual = normalize_quality(m.group(2) or "maj")
    if root_pc is None or qual is None:
        return None
    return root_pc, qual


def _key_from_string(s: str):
    """'C major', 'A:minor', 'Eb' -> (tonic_pc, mode) or (None, None)."""
    s = s.strip()
    mode = "min" if re.search(r"min|m\b|:m", s, re.I) else "maj"
    mroot = re.match(r"([A-G][b#]?)", s)
    return (note_to_pc(mroot.group(1)) if mroot else None), (mode if mroot else None)


# --- McGill Billboard: salami_chords.txt (Harte syntax) ---
def ingest_billboard(raw_dir):
    for path in sorted(glob.glob(os.path.join(raw_dir, "**", "salami_chords.txt"), recursive=True)):
        tonic_pc = mode = None
        chords = []
        with open(path, encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("# tonic:"):
                    tonic_pc = note_to_pc(line.split(":", 1)[1])
                for tok in re.findall(r"[A-G][b#]?:[^\s|]+", line):
                    parsed = parse_harte_chord(tok)
                    if parsed:
                        chords.append(parsed)
        if chords:
            yield {"id": os.path.basename(os.path.dirname(path)),
                   "tonic_pc": tonic_pc, "mode": mode, "chords": chords}


# --- Nottingham: ABC with inline "C" "G7" chord marks ---
def ingest_nottingham(raw_dir):
    for path in sorted(glob.glob(os.path.join(raw_dir, "**", "*.abc"), recursive=True)):
        with open(path, encoding="utf-8", errors="ignore") as f:
            text = f.read()
        for tune in re.split(r"\nX:", text):
            tonic_pc = mode = None
            mk = re.search(r"\nK:\s*([^\n]+)", "\n" + tune)
            if mk:
                tonic_pc, mode = _key_from_string(mk.group(1))
            chords = []
            for sym in re.findall(r'"([^"]+)"', tune):
                parsed = parse_harte_chord(sym.replace("maj", "maj").lstrip("^_"))
                if parsed:
                    chords.append(parsed)
            if chords:
                mid = re.search(r"X:\s*(\d+)", "X:" + tune)
                yield {"id": f"nott-{mid.group(1) if mid else len(chords)}",
                       "tonic_pc": tonic_pc, "mode": mode, "chords": chords}


# --- OpenEWLD: MusicXML <harmony> elements ---
def ingest_openewld(raw_dir):
    import xml.etree.ElementTree as ET
    import zipfile

    def harmonies(root):
        for h in root.iter("harmony"):
            rs = h.find("root/root-step")
            ra = h.find("root/root-alter")
            kind = h.find("kind")
            if rs is None:
                continue
            name = rs.text + ("#" if (ra is not None and ra.text == "1")
                              else "b" if (ra is not None and ra.text == "-1") else "")
            root_pc = note_to_pc(name)
            qtext = (kind.get("text") or kind.text or "maj") if kind is not None else "maj"
            qual = normalize_quality(qtext)
            if root_pc is not None and qual is not None:
                yield root_pc, qual

    for path in sorted(glob.glob(os.path.join(raw_dir, "**", "*.*"), recursive=True)):
        if not path.lower().endswith((".xml", ".musicxml", ".mxl")):
            continue
        try:
            if path.lower().endswith(".mxl"):
                with zipfile.ZipFile(path) as z:
                    inner = next(n for n in z.namelist() if n.endswith((".xml", ".musicxml"))
                                 and not n.startswith("META"))
                    root = ET.fromstring(z.read(inner))
            else:
                root = ET.parse(path).getroot()
        except Exception:
            continue
        chords = list(harmonies(root))
        if chords:
            yield {"id": os.path.splitext(os.path.basename(path))[0],
                   "tonic_pc": None, "mode": None, "chords": chords}


INGESTORS = {
    "billboard": ingest_billboard,
    "nottingham": ingest_nottingham,
    "openewld": ingest_openewld,
}
