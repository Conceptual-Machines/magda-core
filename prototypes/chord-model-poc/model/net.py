"""Tiny GRU next-chord model (Phase 1).

Autoregressive over key-relative chord tokens: embedding -> GRU -> linear head
-> next-token logits. Chord context is short (a handful of chords), so this
stays tiny (tens of k params) while capturing longer-range harmonic structure
(turnarounds, cadences) better than a same-size causal conv would — which is
the whole point of beating the n-gram floor.

Requires torch (see requirements.txt). Import is lazy so the rest of the POC
(representation, n-gram, eval) runs without it.
"""
from __future__ import annotations

import torch
import torch.nn as nn


class ChordGRU(nn.Module):
    def __init__(self, vocab_size, embed=48, hidden=96, layers=1):
        super().__init__()
        self.embed = nn.Embedding(vocab_size, embed, padding_idx=0)
        self.gru = nn.GRU(embed, hidden, num_layers=layers, batch_first=True)
        self.head = nn.Linear(hidden, vocab_size)

    def forward(self, x, h=None):
        e = self.embed(x)
        out, h = self.gru(e, h)
        return self.head(out), h

    def num_params(self):
        return sum(p.numel() for p in self.parameters())


if __name__ == "__main__":
    # Rough size at a plausible vocab (~120 tokens).
    for hidden in (64, 96, 128):
        net = ChordGRU(120, embed=48, hidden=hidden)
        print(f"embed=48 hidden={hidden}  params={net.num_params():,}")
