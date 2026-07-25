"""Plain-float32 forward over the trained checkpoint — the C++ parity reference.

The 4-bit quantization in net.py exists for the FINN/FPGA path; the CPU backend
runs the float master weights (same call as the command model, for the same
reason: it is simpler and at least as accurate). This module is the definition
of "correct" that magda/agents/router_model.cpp must reproduce byte-for-byte,
so its math deliberately mirrors the C++ shape — pad to MAX_LEN, convolve the
whole padded sequence, mean-pool over [0, L).
"""
from __future__ import annotations

import os

import torch
import torch.nn.functional as F

from router.data import load_maps
from router.text import encode


def load_reference(artifacts: str):
    vocab, labels = load_maps(os.path.join(artifacts, "maps.json"))
    ck = torch.load(os.path.join(artifacts, "model.pt"), map_location="cpu")
    sd = ck["state_dict"]

    emb = sd["embed.weight"]
    w1, b1 = sd["b1.0.weight"], sd["b1.0.bias"]
    w2, b2 = sd["b2.0.weight"], sd["b2.0.bias"]
    w3, b3 = sd["b3.0.weight"], sd["b3.0.bias"]
    hw, hb = sd["head.weight"], sd["head.bias"]

    def predict(text: str) -> str:
        _, ids, length = encode(text, vocab)
        if length == 0:
            return ""
        with torch.no_grad():
            e = F.embedding(torch.tensor(ids).unsqueeze(0), emb).transpose(1, 2)
            h = F.relu(F.conv1d(e, w1, b1, padding=1, dilation=1))
            h = F.relu(F.conv1d(h, w2, b2, padding=2, dilation=2))
            h = F.relu(F.conv1d(h, w3, b3, padding=4, dilation=4))
            pooled = h[0][:, :length].mean(1, keepdim=True).transpose(0, 1)
            logits = F.linear(pooled, hw, hb)[0]
        return labels[int(logits.argmax(-1))]

    return vocab, labels, sd, predict
