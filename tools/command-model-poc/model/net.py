"""Quantization-aware intent+slots model (Brevitas) - FINN-mappable.

Design for FPGA dataflow (not a transformer):
  embedding -> 3 dilated 1D-conv blocks (QuantConv1d + QuantReLU) -> two heads
    - intent head: masked mean-pool over time -> QuantLinear -> intent logits
    - slot head:   per-token QuantLinear -> BIO tag logits

Why convs: FINN streams Conv + threshold activations natively; recurrence
(LSTM) and attention do not map cleanly. Three kernel-3 blocks at dilation
1/2/4 give a receptive field of ~15 tokens - covers a whole short command, so
every token sees enough context to be tagged correctly.

Weights/activations are fake-quantized here (QAT). FINN-export hardening
(threading QuantTensors end-to-end, removing the float embedding to a host
lookup / on-chip ROM) is a follow-up on the Linux build box.
"""
from __future__ import annotations

import torch.nn as nn
import brevitas.nn as qnn


class IntentSlotNet(nn.Module):
    def __init__(self, vocab_size, n_intents, n_tags,
                 embed=48, hidden=64, wbits=4, abits=4):
        super().__init__()
        self.embed = nn.Embedding(vocab_size, embed, padding_idx=0)
        self.inq = qnn.QuantIdentity(bit_width=abits)

        def block(ci, co, dilation):
            return nn.Sequential(
                qnn.QuantConv1d(ci, co, kernel_size=3, padding=dilation,
                                dilation=dilation, weight_bit_width=wbits, bias=True),
                qnn.QuantReLU(bit_width=abits),
            )

        self.b1 = block(embed, hidden, 1)
        self.b2 = block(hidden, hidden, 2)
        self.b3 = block(hidden, hidden, 4)
        self.slot_head = qnn.QuantLinear(hidden, n_tags, weight_bit_width=wbits, bias=True)
        self.intent_head = qnn.QuantLinear(hidden, n_intents, weight_bit_width=wbits, bias=True)

    def forward(self, x, mask=None):
        e = self.embed(x).transpose(1, 2)          # [B, embed, L]
        h = self.b3(self.b2(self.b1(self.inq(e))))  # [B, hidden, L]

        slot_logits = self.slot_head(h.transpose(1, 2))  # [B, L, n_tags]

        if mask is not None:
            m = mask.unsqueeze(1).float()           # [B, 1, L]
            pooled = (h * m).sum(2) / m.sum(2).clamp(min=1)
        else:
            pooled = h.mean(2)
        intent_logits = self.intent_head(pooled)    # [B, n_intents]
        return intent_logits, slot_logits
