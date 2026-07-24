"""Data pipeline for the intent+slots model: rows -> token ids + intent + tags.

Reuses dataset.tagging (the same tagger whose round-trip we verified). Builds
small vocab / intent / tag maps from the training split and persists them so
inference and FINN export use identical indices.

Key trick - OOV name augmentation: track names are open-vocab (users invent
them), so during training we randomly replace NAME-slot tokens with <UNK>. That
forces the model to tag names by CONTEXT ("the word before 'track'") instead of
memorising the 23 names in the synthetic set - exactly what makes it generalise
to unseen names. PLUGIN/COLOR are NOT augmented: they're resolved by lookup, so
their surface identity must stay recognisable.
"""
from __future__ import annotations

import json
import os
import random
import sys

import torch
from torch.utils.data import Dataset

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from dataset.tagging import tag, tokenize  # noqa: E402

PAD, UNK, ALIAS = "<PAD>", "<UNK>", "<alias>"
MAX_LEN = 24
IGNORE = -100
NAME_SLOTS = {"TRACK_NAME", "NEW_NAME", "GROUP_NAME"}  # copy-through -> safe to UNK


def canon(token: str) -> str:
    """Preprocess-ahead: collapse every plugin reference to one opaque token so
    the model never learns plugin identities. A host step rewrites '@serum',
    '@vital', '@anything' -> '<alias>', and any plugin-param reference
    ('@serum.cutoff') -> the single opaque '<alias.param>' token. The model only
    tags the placeholder's position/role; it never sees which plugin or which
    param. The reconstructor substitutes the real identities back by position
    (1st @-mention -> its alias, 1st PARAM span -> param1, ...). This is why an
    unseen alias generalises for free and the vocab never grows with the plugin
    registry. Data files keep the readable '@serum' surface so the tagger/
    reconstructor can still recover the true identity."""
    if token.startswith("@"):
        return "<alias.param>" if "." in token else ALIAS
    return token.lower()


def load_rows(path):
    return [json.loads(l) for l in open(path, encoding="utf-8") if l.strip()]


def build_maps(rows):
    vocab = {PAD: 0, UNK: 1, ALIAS: 2}  # ALIAS reserved: all @-refs collapse here
    intents, tags = {}, {"O": 0}
    for r in rows:
        intent, toks, tg = tag(r["input"], r["actions"])
        for t in toks:
            tl = canon(t)
            if tl not in vocab:
                vocab[tl] = len(vocab)
        intents.setdefault(intent, len(intents))
        for x in tg:
            tags.setdefault(x, len(tags))
    return vocab, intents, tags


def save_maps(path, vocab, intents, tags):
    json.dump({"vocab": vocab, "intents": intents, "tags": tags, "max_len": MAX_LEN},
              open(path, "w", encoding="utf-8"), ensure_ascii=False)


def load_maps(path):
    m = json.load(open(path, encoding="utf-8"))
    return m["vocab"], m["intents"], m["tags"]


class CmdDataset(Dataset):
    def __init__(self, rows, vocab, intents, tags, unk_aug=0.0, seed=0):
        self.vocab, self.intents, self.tags = vocab, intents, tags
        self.unk_aug = unk_aug
        self.rng = random.Random(seed)
        self.items = []
        for r in rows:
            intent, toks, tg = tag(r["input"], r["actions"])
            self.items.append((toks, intent, tg))

    def __len__(self):
        return len(self.items)

    def _encode_tokens(self, toks, tg):
        ids = []
        for t, x in zip(toks[:MAX_LEN], tg[:MAX_LEN]):
            wid = self.vocab.get(canon(t), self.vocab[UNK])
            if self.unk_aug and x[2:] in NAME_SLOTS and self.rng.random() < self.unk_aug:
                wid = self.vocab[UNK]
            ids.append(wid)
        return ids

    def __getitem__(self, i):
        toks, intent, tg = self.items[i]
        ids = self._encode_tokens(toks, tg)
        tag_ids = [self.tags[x] for x in tg[:MAX_LEN]]
        L = len(ids)
        ids += [0] * (MAX_LEN - L)
        tag_ids += [IGNORE] * (MAX_LEN - L)
        mask = [1] * L + [0] * (MAX_LEN - L)
        return (torch.tensor(ids), torch.tensor(self.intents[intent]),
                torch.tensor(tag_ids), torch.tensor(mask))


def encode_text(text, vocab):
    """Inference-side: surface tokens + padded id tensor + length."""
    toks = tokenize(text)[:MAX_LEN]
    ids = [vocab.get(canon(t), vocab[UNK]) for t in toks]
    L = len(ids)
    ids += [0] * (MAX_LEN - L)
    return toks, torch.tensor(ids).unsqueeze(0), L
