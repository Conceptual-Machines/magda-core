"""Train the Brevitas intent+slots model; select on END-TO-END exact DSL match.

Loss = intent cross-entropy + token-level slot cross-entropy (PAD ignored).
But model selection uses the metric that actually matters: predict -> reconstruct
-> dsl.render == gold, on the val split. Best checkpoint + maps land in
model/artifacts/ for eval.run --torch-model and (later) FINN export.

    python -m model.train_intent_slots --epochs 40 --unk-aug 0.3
"""
from __future__ import annotations

import argparse
import os
import sys

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from model.data import (CmdDataset, IGNORE, build_maps, load_rows, save_maps)  # noqa: E402
from model.net import IntentSlotNet  # noqa: E402
from model.intent_slot_parser import predict_dsl  # noqa: E402
from magda_dsl import dsl  # noqa: E402

HERE = os.path.dirname(__file__)


def e2e_exact(model, rows, vocab, id2intent, id2tag):
    ok = 0
    for r in rows:
        pred = predict_dsl(model, r["input"], vocab, id2intent, id2tag)
        ok += dsl.normalize(pred) == dsl.normalize(r["output"])
    return ok / max(len(rows), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default=os.path.join(HERE, "..", "data", "train.jsonl"))
    ap.add_argument("--val", default=os.path.join(HERE, "..", "data", "val.jsonl"))
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--embed", type=int, default=48)
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--wbits", type=int, default=4)
    ap.add_argument("--abits", type=int, default=4)
    ap.add_argument("--unk-aug", type=float, default=0.3, help="prob of UNK-ing NAME tokens")
    ap.add_argument("--out", default=os.path.join(HERE, "artifacts"))
    args = ap.parse_args()

    torch.manual_seed(7)
    rows_train = load_rows(os.path.abspath(args.train))
    rows_val = load_rows(os.path.abspath(args.val))

    vocab, intents, tags = build_maps(rows_train)
    os.makedirs(args.out, exist_ok=True)
    save_maps(os.path.join(args.out, "maps.json"), vocab, intents, tags)
    id2intent = {v: k for k, v in intents.items()}
    id2tag = {v: k for k, v in tags.items()}
    print(f"vocab={len(vocab)} intents={len(intents)} tags={len(tags)} "
          f"train={len(rows_train)} val={len(rows_val)}")

    ds_tr = CmdDataset(rows_train, vocab, intents, tags, unk_aug=args.unk_aug, seed=7)
    ds_va = CmdDataset(rows_val, vocab, intents, tags, unk_aug=0.0)
    dl = DataLoader(ds_tr, batch_size=args.batch, shuffle=True)

    hp = {"embed": args.embed, "hidden": args.hidden, "wbits": args.wbits, "abits": args.abits}
    model = IntentSlotNet(len(vocab), len(intents), len(tags), **hp)
    n_params = sum(p.numel() for p in model.parameters())
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    n_tags = len(tags)

    best = -1.0
    for epoch in range(1, args.epochs + 1):
        model.train()
        tot = 0.0
        for ids, intent, tagids, mask in dl:
            il, sl = model(ids, mask)
            loss = (F.cross_entropy(il, intent) +
                    F.cross_entropy(sl.reshape(-1, n_tags), tagids.reshape(-1), ignore_index=IGNORE))
            opt.zero_grad()
            loss.backward()
            opt.step()
            tot += float(loss)
        val = e2e_exact(model, rows_val, vocab, id2intent, id2tag)
        flag = ""
        if val > best:
            best = val
            torch.save({"state_dict": model.state_dict(), "hp": hp},
                       os.path.join(args.out, "model.pt"))
            flag = "  *saved"
        print(f"epoch {epoch:2d}  loss={tot/len(dl):.3f}  val_e2e={val:.1%}{flag}")

    print(f"\nbest val end-to-end exact = {best:.1%}  | params={n_params:,} "
          f"({args.wbits}-bit weights ~= {n_params*args.wbits/8/1024:.1f} KB)")


if __name__ == "__main__":
    main()
