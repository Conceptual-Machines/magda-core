"""Rows -> token ids + label id, plus the vocab/label maps.

Word dropout is the one augmentation that matters here. The router sees real
user prose, not templates, so most incoming messages contain words that are not
in the (closed, template-derived) vocab and arrive as <UNK>. Randomly UNK-ing
training tokens forces the model to classify from the words it *does* know
rather than assuming full coverage — the router equivalent of the command
model's OOV name augmentation.
"""
from __future__ import annotations

import gzip
import json
import os
import random
import sys

import torch
from torch.utils.data import Dataset

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from router.labels import LABELS  # noqa: E402
from router.text import ALIAS, ALIAS_PARAM, MAX_LEN, PAD, UNK, canon, encode, tokenize  # noqa: E402


def open_rows(path, mode="rt"):
    """Open a .jsonl or .jsonl.gz transparently.

    The generated splits are committed (they define exactly what the shipped
    weights were trained on), and templated text compresses ~6.5x — 7.8 MB of
    plain JSONL becomes ~1.1 MB. Worth it for something regenerable but
    auditable. The hand-authored eval/testset.jsonl stays uncompressed: it is
    small and meant to be read in review.
    """
    if path.endswith(".gz"):
        return gzip.open(path, mode, encoding="utf-8")
    return open(path, mode, encoding="utf-8")


def resolve_rows(path):
    """Accept either spelling — .jsonl.gz wins if both are absent/present."""
    if os.path.exists(path):
        return path
    alt = path[:-3] if path.endswith(".gz") else path + ".gz"
    return alt if os.path.exists(alt) else path


def load_rows(path):
    with open_rows(resolve_rows(path)) as f:
        return [json.loads(l) for l in f if l.strip()]


def build_vocab(rows, min_count=1):
    counts = {}
    for r in rows:
        for t in tokenize(r["input"])[:MAX_LEN]:
            k = canon(t)
            counts[k] = counts.get(k, 0) + 1
    vocab = {PAD: 0, UNK: 1, ALIAS: 2, ALIAS_PARAM: 3}
    for tok in sorted(counts):  # sorted -> stable ids across runs
        if counts[tok] >= min_count and tok not in vocab:
            vocab[tok] = len(vocab)
    return vocab


def save_maps(path, vocab):
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"vocab": vocab, "labels": LABELS, "max_len": MAX_LEN},
                  f, ensure_ascii=False)


def load_maps(path):
    with open(path, encoding="utf-8") as f:
        m = json.load(f)
    return m["vocab"], m["labels"]


class RouterDataset(Dataset):
    def __init__(self, rows, vocab, drop=0.0, seed=0):
        self.vocab = vocab
        self.drop = drop
        self.rng = random.Random(seed)
        self.items = []
        for r in rows:
            _, ids, length = encode(r["input"], vocab)
            self.items.append((ids, length, LABELS.index(r["label"])))

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        ids, length, label = self.items[i]
        if self.drop:
            ids = [self.vocab[UNK] if t < length and self.rng.random() < self.drop else w
                   for t, w in enumerate(ids)]
        mask = [1] * length + [0] * (MAX_LEN - length)
        return torch.tensor(ids), torch.tensor(label), torch.tensor(mask)
