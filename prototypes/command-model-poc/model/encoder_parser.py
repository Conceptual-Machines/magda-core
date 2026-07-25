"""Inference for the pretrained-encoder model: text -> DSL string.

Identical contract to `model/intent_slot_parser.py` (the conv-net path): the
model does perception, the deterministic `reconstruct` + `dsl.render` build the
DSL. Only the perception half differs, so the two are directly comparable under
`eval.run`.
"""
from __future__ import annotations

import os
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from model.data_encoder import encode_text, load_maps  # noqa: E402
from model.net_encoder import EncoderIntentSlotNet, load_tokenizer  # noqa: E402
from dataset.tagging import reconstruct  # noqa: E402
from magda_dsl import dsl  # noqa: E402

ARTIFACTS = os.path.join(os.path.dirname(__file__), "artifacts_encoder")


def predict_dsl(model, text, tokenizer, id2intent, id2tag, device="cpu"):
    words, ids, attn, first = encode_text(text, tokenizer)
    if not words:
        return ""
    model.eval()
    with torch.no_grad():
        intent_logits, slot_logits = model(ids.to(device), attn.to(device))
    intent = id2intent[int(intent_logits.argmax(-1))]
    # One tag per WORD, read at that word's first subword. Words truncated away
    # fall back to "O" so the tag sequence always lines up with `words`.
    tag_ids = slot_logits.argmax(-1)[0].tolist()
    tags = [id2tag[tag_ids[pos]] if pos >= 0 else "O" for pos in first]
    try:
        return dsl.render(reconstruct(intent, words, tags))
    except Exception:
        return ""


def build_parser(artifacts=ARTIFACTS, device="cpu"):
    """Load maps + checkpoint, return a `str -> DSL str` function for eval.run."""
    intents, tags, model_name = load_maps(os.path.join(artifacts, "maps.json"))
    id2intent = {v: k for k, v in intents.items()}
    id2tag = {v: k for k, v in tags.items()}
    tokenizer = load_tokenizer(model_name)
    model = EncoderIntentSlotNet(model_name, len(intents), len(tags), tokenizer=tokenizer)
    state = torch.load(os.path.join(artifacts, "model.pt"), map_location="cpu")
    model.load_state_dict(state["state_dict"])
    model.to(device).eval()
    return lambda text: predict_dsl(model, text, tokenizer, id2intent, id2tag, device)
