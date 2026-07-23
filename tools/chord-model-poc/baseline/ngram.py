"""Interpolated n-gram next-chord model (Phase 0 floor).

Chord n-grams are a strong baseline; the neural model has to beat this to earn
its keep. Uses simple linear interpolation over orders 1..n with add-k
smoothing on the unigram — plenty for a floor, no external deps.
"""
from __future__ import annotations

import math
from collections import defaultdict

from chords.vocab import BOS, EOS


class NGramModel:
    def __init__(self, order: int = 3, add_k: float = 0.01):
        self.order = order
        self.add_k = add_k
        self.counts = [defaultdict(lambda: defaultdict(int)) for _ in range(order)]
        self.vocab: set[str] = set()

    def _pad(self, seq):
        return [BOS] * (self.order - 1) + list(seq) + [EOS]

    def train(self, sequences):
        for seq in sequences:
            s = self._pad(seq)
            self.vocab.update(seq)
            self.vocab.add(EOS)
            for i in range(self.order - 1, len(s)):
                nxt = s[i]
                for o in range(self.order):
                    ctx = tuple(s[i - o:i])
                    self.counts[o][ctx][nxt] += 1
        return self

    def _order_dist(self, o, ctx):
        table = self.counts[o].get(ctx)
        if not table:
            return None
        tot = sum(table.values())
        return {w: c / tot for w, c in table.items()}

    def next_dist(self, context) -> dict[str, float]:
        """Interpolated P(next | context) over all vocab words."""
        ctx_full = tuple(([BOS] * (self.order - 1) + list(context))[-(self.order - 1):]) \
            if self.order > 1 else tuple()
        # higher orders weighted more; missing contexts contribute nothing.
        weights = [2 ** o for o in range(self.order)]
        dist = defaultdict(float)
        wsum = 0.0
        for o in range(self.order):
            ctx = ctx_full[len(ctx_full) - o:] if o else tuple()
            d = self._order_dist(o, ctx)
            if d is None:
                continue
            w = weights[o]
            wsum += w
            for word, p in d.items():
                dist[word] += w * p
        V = max(len(self.vocab), 1)
        if wsum == 0:  # wholly unseen context -> uniform-ish add-k
            return {w: 1.0 / V for w in self.vocab}
        # normalize + tiny add-k floor so held-out words never get exactly 0
        for w in self.vocab:
            dist[w] = (dist[w] / wsum) + self.add_k / V
        z = sum(dist.values())
        return {w: p / z for w, p in dist.items()}

    def topk(self, context, k=3):
        d = self.next_dist(context)
        return sorted(d.items(), key=lambda kv: kv[1], reverse=True)[:k]

    def perplexity(self, sequences) -> float:
        nll, n = 0.0, 0
        for seq in sequences:
            s = self._pad(seq)
            for i in range(self.order - 1, len(s)):
                ctx = s[self.order - 1:i]  # actual history (post-padding)
                p = self.next_dist(ctx).get(s[i], 1e-9)
                nll += -math.log(max(p, 1e-12))
                n += 1
        return math.exp(nll / n) if n else float("inf")
