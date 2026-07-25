"""Train the router classifier; select on held-out accuracy.

    python -m router.train --epochs 30 --drop 0.15

Best checkpoint + maps land in artifacts/ for evaluate.py and export_cpp.py.
"""
from __future__ import annotations

import argparse
import os
import sys

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from router.data import RouterDataset, build_vocab, load_rows, save_maps  # noqa: E402
from router.labels import LABELS  # noqa: E402
from router.net import IntentNet  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)


def accuracy(model, loader):
    model.eval()
    ok = n = 0
    with torch.no_grad():
        for ids, label, mask in loader:
            pred = model(ids, mask).argmax(-1)
            ok += int((pred == label).sum())
            n += len(label)
    return ok / max(n, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default=os.path.join(POC, "data", "train.jsonl.gz"))
    ap.add_argument("--val", default=os.path.join(POC, "data", "val.jsonl.gz"))
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--embed", type=int, default=32)
    ap.add_argument("--hidden", type=int, default=48)
    ap.add_argument("--wbits", type=int, default=4)
    ap.add_argument("--abits", type=int, default=4)
    ap.add_argument("--drop", type=float, default=0.15, help="word-dropout probability")
    ap.add_argument("--min-count", type=int, default=1, help="vocab frequency floor")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default=os.path.join(POC, "artifacts"))
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    rows_train = load_rows(args.train)
    rows_val = load_rows(args.val)

    vocab = build_vocab(rows_train, args.min_count)
    os.makedirs(args.out, exist_ok=True)
    save_maps(os.path.join(args.out, "maps.json"), vocab)
    print(f"vocab={len(vocab)} labels={len(LABELS)} "
          f"train={len(rows_train)} val={len(rows_val)}")

    dl_tr = DataLoader(RouterDataset(rows_train, vocab, drop=args.drop, seed=args.seed),
                       batch_size=args.batch, shuffle=True)
    dl_va = DataLoader(RouterDataset(rows_val, vocab), batch_size=args.batch)

    # COMMAND and BOTH are richly parameterized and generate far more rows than
    # MIXING or SESSION, which are mostly slot-free questions. Inverse-frequency
    # weights stop the loss from being dominated by the big classes — a router
    # that never predicts SESSION would score well on accuracy and be useless.
    counts = torch.tensor([sum(r["label"] == x for r in rows_train) for x in LABELS],
                          dtype=torch.float)
    weight = counts.sum() / (len(LABELS) * counts.clamp(min=1))
    print("  class rows: " + ", ".join(f"{x}={int(c)}" for x, c in zip(LABELS, counts)))

    hp = {"embed": args.embed, "hidden": args.hidden,
          "wbits": args.wbits, "abits": args.abits}
    model = IntentNet(len(vocab), len(LABELS), **hp)
    n_params = sum(p.numel() for p in model.parameters())
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)

    best = -1.0
    for epoch in range(1, args.epochs + 1):
        model.train()
        tot = 0.0
        for ids, label, mask in dl_tr:
            loss = F.cross_entropy(model(ids, mask), label, weight=weight)
            opt.zero_grad()
            loss.backward()
            opt.step()
            tot += float(loss)
        val = accuracy(model, dl_va)
        flag = ""
        if val > best:
            best = val
            torch.save({"state_dict": model.state_dict(), "hp": hp},
                       os.path.join(args.out, "model.pt"))
            flag = "  *saved"
        print(f"epoch {epoch:2d}  loss={tot/len(dl_tr):.4f}  val={val:.2%}{flag}")

    emb = len(vocab) * args.embed
    print(f"\nbest val = {best:.2%}  | params={n_params:,} "
          f"({emb:,} of them embedding) ~= {n_params*4/1024:.0f} KB as float32")


if __name__ == "__main__":
    main()
