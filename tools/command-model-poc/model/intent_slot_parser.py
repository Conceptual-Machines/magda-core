"""Inference: text -> intent+tags -> reconstruct -> dsl.render -> DSL string.

This is the FPGA pipeline in software: the model does perception (intent +
per-token tags) and the deterministic reconstruct/render (the same functions we
round-trip-verified) emit the DSL. Used both as the eval parser and for the
train-time end-to-end metric. On the KV260 this split is literal: net on
fabric, reconstruct/render on the A53.
"""
from __future__ import annotations

import os
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from model.data import MAX_LEN, encode_text, load_maps  # noqa: E402
from model.net import IntentSlotNet  # noqa: E402
from dataset.tagging import reconstruct  # noqa: E402
from magda_dsl import dsl  # noqa: E402

ARTIFACTS = os.path.join(os.path.dirname(__file__), "artifacts")


def predict_dsl(model, text, vocab, id2intent, id2tag):
    toks, ids, L = encode_text(text, vocab)
    if L == 0:
        return ""
    mask = torch.zeros(1, MAX_LEN)
    mask[0, :L] = 1.0
    model.eval()
    with torch.no_grad():
        intent_logits, slot_logits = model(ids, mask)
    intent = id2intent[int(intent_logits.argmax(-1))]
    tag_ids = slot_logits.argmax(-1)[0][:L].tolist()
    tags = [id2tag[t] for t in tag_ids]
    try:
        return dsl.render(reconstruct(intent, toks[:L], tags))
    except Exception:
        return ""


def build_parser(artifacts=ARTIFACTS, hp=None):
    """Load maps + checkpoint, return a `str -> DSL str` function for eval.run."""
    vocab, intents, tags = load_maps(os.path.join(artifacts, "maps.json"))
    id2intent = {v: k for k, v in intents.items()}
    id2tag = {v: k for k, v in tags.items()}
    ckpt = torch.load(os.path.join(artifacts, "model.pt"), map_location="cpu")
    hp = hp or ckpt.get("hp", {})
    model = IntentSlotNet(len(vocab), len(intents), len(tags),
                          embed=hp.get("embed", 48), hidden=hp.get("hidden", 64),
                          wbits=hp.get("wbits", 4), abits=hp.get("abits", 4))
    model.load_state_dict(ckpt["state_dict"])
    model.eval()
    return lambda text: predict_dsl(model, text, vocab, id2intent, id2tag)
