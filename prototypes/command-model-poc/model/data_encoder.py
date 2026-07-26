"""Data pipeline for the pretrained-encoder model: subword <-> word alignment.

The one real detail in the encoder swap (#1847): transformers tokenize to
SUBWORDS, but `dataset/tagging.py` emits one BIO tag per WORD, and
`reconstruct()` consumes word-level tags. Standard fix, applied here:

  training   label the FIRST subword of each word with that word's tag; every
             other subword gets IGNORE so it contributes no loss.
  inference  read the slot logits at each word's first-subword position, which
             yields exactly one tag per word -> `reconstruct()` is unchanged.

Word segmentation stays `dataset.tagging.tokenize` (the same regex the tagger
and the reconstructor use), so a word index means the same thing on both sides.

Unlike `model/data.py` there is no vocab to build and no UNK augmentation: an
unseen track name is subword-tokenized into pieces the encoder has seen, which
is the mechanism that was being approximated by randomly UNK-ing name tokens.
Case is preserved — these encoders are cased, and capitalisation is real
evidence about what is a name.
"""
from __future__ import annotations

import json
import os
import sys

import torch
from torch.utils.data import Dataset

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from dataset.tagging import tag, tokenize  # noqa: E402

IGNORE = -100
MAX_LEN = 64          # subwords, not words; commands are short
ALIAS, ALIAS_PARAM = "<alias>", "<alias.param>"


def canon(token: str) -> str:
    """Collapse plugin references to opaque placeholders, keeping case.

    Same contract as `model/data.py:canon` — the model never learns plugin
    identities and the reconstructor substitutes the real alias back by
    position — minus the `.lower()`, which only existed to keep the
    from-scratch model's 291-word vocab small.
    """
    if token.startswith("@"):
        return ALIAS_PARAM if "." in token else ALIAS
    return token


def load_rows(path):
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def build_label_maps(rows):
    """Intent and tag id maps. No vocab — the pretrained tokenizer is it."""
    intents, tags = {}, {"O": 0}
    for r in rows:
        intent, _, tg = tag(r["input"], r["actions"])
        intents.setdefault(intent, len(intents))
        for x in tg:
            tags.setdefault(x, len(tags))
    return intents, tags


def save_maps(path, intents, tags, model_name):
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"intents": intents, "tags": tags, "model_name": model_name,
                   "max_len": MAX_LEN},
                  f, ensure_ascii=False)


def load_maps(path):
    with open(path, encoding="utf-8") as f:
        m = json.load(f)
    return m["intents"], m["tags"], m["model_name"]


def encode_words(words, tokenizer, max_len=MAX_LEN):
    """Words -> (encoding, first_subword_index_per_word).

    Returns the index in the subword sequence where each word starts, or -1 if
    the word fell outside max_len. That index is the read-out point for the
    word's tag at inference and the write point for its label at training time.
    """
    enc = tokenizer([canon(w) for w in words], is_split_into_words=True,
                    truncation=True, max_length=max_len,
                    padding="max_length", return_tensors=None)
    word_ids = enc.word_ids(0) if hasattr(enc, "word_ids") else enc.word_ids()
    first = [-1] * len(words)
    prev = None
    for pos, wid in enumerate(word_ids):
        if wid is not None and wid != prev and first[wid] == -1:
            first[wid] = pos
        prev = wid
    return enc, first


class EncoderCmdDataset(Dataset):
    def __init__(self, rows, tokenizer, intents, tags):
        self.tokenizer, self.intents, self.tags = tokenizer, intents, tags
        self.items = [tag(r["input"], r["actions"]) for r in rows]

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        intent, words, tg = self.items[i]
        enc, first = encode_words(words, self.tokenizer)
        labels = [IGNORE] * len(enc["input_ids"])
        for w, pos in enumerate(first):
            if pos >= 0 and w < len(tg):
                labels[pos] = self.tags.get(tg[w], 0)
        return (torch.tensor(enc["input_ids"]),
                torch.tensor(enc["attention_mask"]),
                torch.tensor(self.intents[intent]),
                torch.tensor(labels))


def encode_text(text, tokenizer):
    """Inference-side: surface words + model inputs + per-word read-out index."""
    words = tokenize(text)
    enc, first = encode_words(words, tokenizer)
    return (words,
            torch.tensor(enc["input_ids"]).unsqueeze(0),
            torch.tensor(enc["attention_mask"]).unsqueeze(0),
            first)
