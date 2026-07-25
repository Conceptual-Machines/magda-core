"""Tokenizer + case folding for the router model — the C++ parity contract.

This is deliberately NOT the command-model tokenizer. That one is ASCII-only
(`[@#]?[A-Za-z0-9_'-]+`), which silently drops every Japanese, Russian and
Chinese character: a localized request tokenizes to the empty sequence. The
command model gets away with it (its shipped vocab has zero non-ASCII tokens —
it is English-only in practice). The router cannot: it runs on *every* console
turn, so a Japanese user would have all traffic land on the default label.

So the router defines its own multilingual rule, small enough to reimplement
byte-for-byte in C++ (magda/agents/router_model.cpp) with no ICU dependency:

  * CJK ideographs and kana are **one token per codepoint** — those scripts are
    written without spaces, so character unigrams are the only segmentation
    available without a dictionary. The dilated conv stack (receptive field ~15)
    reads a whole short request anyway.
  * Everything else is a maximal run of "word" codepoints: ASCII alphanumerics
    plus `_ ' -`, and any non-ASCII codepoint that is not CJK and not in a
    punctuation/symbol block (so Cyrillic, Greek, Hangul and accented Latin form
    ordinary word runs).
  * A leading `@`/`#` sigil and a trailing decimal (`0.5`) stay attached, as in
    the command model — `@serum` must survive as one token to be collapsed.

Case folding covers ASCII, Latin-1 supplement and Cyrillic. That is every cased
script in MAGDA's locale set (en/ru + accented Latin); kana and CJK are
caseless. Anything else passes through unfolded rather than pulling in a full
Unicode case table that C++ would then have to mirror.
"""
from __future__ import annotations

MAX_LEN = 32
PAD, UNK, ALIAS, ALIAS_PARAM = "<PAD>", "<UNK>", "<alias>", "<alias.param>"

# Per-codepoint tokens: kana, CJK ideographs (+ extensions), halfwidth kana.
CJK_RANGES = (
    (0x3040, 0x30FF),    # hiragana + katakana
    (0x31F0, 0x31FF),    # katakana phonetic extensions
    (0x3400, 0x4DBF),    # CJK ext A
    (0x4E00, 0x9FFF),    # CJK unified ideographs
    (0xF900, 0xFAFF),    # CJK compatibility ideographs
    (0xFF66, 0xFF9F),    # halfwidth katakana
    (0x20000, 0x2FA1F),  # CJK ext B..F + compat supplement
)

# Non-ASCII blocks that are punctuation/symbols rather than letters.
SYMBOL_RANGES = (
    (0x2000, 0x206F),    # general punctuation
    (0x2190, 0x2BFF),    # arrows, math, misc symbols
    (0x3000, 0x303F),    # CJK punctuation
    (0xFE00, 0xFE6F),    # variation selectors, small forms
    (0xFF01, 0xFF65),    # fullwidth ASCII variants
    (0xFFA0, 0xFFFF),    # halfwidth forms, specials
    (0x1F000, 0x1FFFF),  # emoji + pictographs
)


def _in(cp: int, ranges) -> bool:
    return any(lo <= cp <= hi for lo, hi in ranges)


def is_cjk(cp: int) -> bool:
    return _in(cp, CJK_RANGES)


def is_word(cp: int) -> bool:
    """A codepoint that can appear inside a multi-character word token."""
    if cp < 0x80:
        return (0x41 <= cp <= 0x5A or 0x61 <= cp <= 0x7A or 0x30 <= cp <= 0x39
                or cp in (0x5F, 0x27, 0x2D))  # _ ' -
    return not is_cjk(cp) and not _in(cp, SYMBOL_RANGES)


def fold(text: str) -> str:
    """Lowercase ASCII + Latin-1 supplement + Cyrillic; pass the rest through."""
    out = []
    for ch in text:
        cp = ord(ch)
        if 0x41 <= cp <= 0x5A:                                  # A-Z
            cp += 0x20
        elif 0xC0 <= cp <= 0xDE and cp != 0xD7:                 # À-Þ (not ×)
            cp += 0x20
        elif 0x410 <= cp <= 0x42F:                              # А-Я
            cp += 0x20
        elif 0x400 <= cp <= 0x40F:                              # Ѐ-Џ (incl. Ё)
            cp += 0x50
        out.append(chr(cp))
    return "".join(out)


def expand_alias_brackets(text: str) -> str:
    """The app rewrites '@mention' into the DSL alias token '<mention>' before
    an agent sees it. Restore the '@' surface the model was trained on. No-op
    on plain text, so the committed fixtures stay byte-stable."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text[i] == "<":
            j = i + 1
            while j < n and is_word(ord(text[j])) and ord(text[j]) < 0x80:
                j += 1
            if j > i + 1 and j < n and text[j] == ">":
                out.append("@" + text[i + 1:j])
                i = j + 1
                continue
        out.append(text[i])
        i += 1
    return "".join(out)


def tokenize(text: str):
    """Surface tokens, identity-preserving. Mirrors RouterModel::tokenize()."""
    toks = []
    i, n = 0, len(text)
    while i < n:
        if is_cjk(ord(text[i])):
            toks.append(text[i])
            i += 1
            continue
        start = j = i
        if text[j] in "@#":
            j += 1
        core = j
        while j < n and is_word(ord(text[j])):
            j += 1
        if j == core:  # nothing but a sigil / separator here
            i += 1
            continue
        if j + 1 < n and text[j] == "." and "0" <= text[j + 1] <= "9":
            j += 1
            while j < n and "0" <= text[j] <= "9":
                j += 1
        toks.append(text[start:j])
        i = j
    return toks


def canon(token: str) -> str:
    """Vocab key for a surface token. Plugin references collapse to one opaque
    token so the router never learns plugin identities (and the vocab never
    grows with the plugin registry) — same trick as the command model."""
    if token.startswith("@"):
        return ALIAS_PARAM if "." in token else ALIAS
    return fold(token)


def encode(text: str, vocab: dict):
    """Inference-side: surface tokens + padded ids + true length."""
    toks = tokenize(expand_alias_brackets(text))[:MAX_LEN]
    ids = [vocab.get(canon(t), vocab[UNK]) for t in toks]
    length = len(ids)
    ids += [0] * (MAX_LEN - length)
    return toks, ids, length
