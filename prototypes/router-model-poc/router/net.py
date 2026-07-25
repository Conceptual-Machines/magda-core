"""The router net: the command model's IntentSlotNet minus the slot head.

  embedding -> 3 dilated 1D-conv blocks (QuantConv1d + QuantReLU)
            -> masked mean-pool -> QuantLinear -> ConsoleIntent logits

Same topology, same reasons (see command-model-poc/model/net.py): convs and
threshold activations stream natively on FINN, so the FPGA path stays open;
kernel-3 blocks at dilation 1/2/4 give a ~15-token receptive field, which spans
a whole console request.

There is no slot head because the router extracts nothing — it answers "which
agent", and the agent behind it does the parsing. That also makes it a third of
the size: no per-token projection, and a 7-way head instead of 38-way.
"""
from __future__ import annotations

import brevitas.nn as qnn
import torch.nn as nn


class IntentNet(nn.Module):
    def __init__(self, vocab_size, n_labels, embed=32, hidden=48, wbits=4, abits=4):
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
        self.head = qnn.QuantLinear(hidden, n_labels, weight_bit_width=wbits, bias=True)

    def forward(self, x, mask=None):
        e = self.embed(x).transpose(1, 2)            # [B, embed, L]
        h = self.b3(self.b2(self.b1(self.inq(e))))   # [B, hidden, L]
        if mask is not None:
            m = mask.unsqueeze(1).float()
            pooled = (h * m).sum(2) / m.sum(2).clamp(min=1)
        else:
            pooled = h.mean(2)
        return self.head(pooled)                     # [B, n_labels]
