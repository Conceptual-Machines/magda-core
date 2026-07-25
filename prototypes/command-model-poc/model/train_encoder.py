"""Fine-tune a pretrained encoder for intent+slots; select on END-TO-END exact.

Mirrors `train_intent_slots.py` so the two runs are comparable: same rows, same
loss shape (intent CE + token CE), same end-to-end metric (predict ->
reconstruct -> render == gold). Two things change: the encoder, and what the
checkpoint is selected on.

Three sets are scored every epoch, and the difference matters:

    val   template-derived, saturates at 100% -> ranks checkpoints by nothing
    dev   hand-authored held-out phrasing     -> THIS selects the checkpoint
    ood   hand-authored, sealed               -> printed only, never selected on

Selecting on val kept the worse checkpoint in three runs out of three (#1847).
Selecting on ood would be worse still: it would convert the report set from a
prediction about future input into a description of its own 71 rows.

    python -m model.train_encoder --preset xlmr --epochs 3
    python -m model.train_encoder --preset distilmbert --epochs 4 --batch 32
    python -m model.train_encoder --preset xlmr --select-on val   # old behaviour
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from model.data_encoder import (IGNORE, EncoderCmdDataset, build_label_maps,  # noqa: E402
                                load_rows, save_maps)
from model.net_encoder import EncoderIntentSlotNet, PRESETS, load_tokenizer  # noqa: E402
from model.encoder_parser import predict_dsl  # noqa: E402
from magda_dsl import dsl  # noqa: E402

HERE = os.path.dirname(__file__)


def pick_device(requested):
    if requested != "auto":
        return requested
    if torch.backends.mps.is_available():
        return "mps"
    return "cuda" if torch.cuda.is_available() else "cpu"


def e2e_exact(model, rows, tokenizer, id2intent, id2tag, device):
    ok = 0
    for r in rows:
        pred = predict_dsl(model, r["input"], tokenizer, id2intent, id2tag, device)
        ok += dsl.normalize(pred) == dsl.normalize(r["output"])
    return ok / max(len(rows), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="xlmr", help=f"one of {sorted(PRESETS)} or a HF model id")
    ap.add_argument("--train", default=os.path.join(HERE, "..", "data", "train.jsonl"))
    ap.add_argument("--val", default=os.path.join(HERE, "..", "data", "val.jsonl"))
    ap.add_argument("--dev", default=os.path.join(HERE, "..", "eval", "dev_testset.jsonl"),
                    help="hand-authored held-out phrasing; THIS is what selects the checkpoint")
    ap.add_argument("--ood", default=os.path.join(HERE, "..", "eval", "ood_testset.jsonl"),
                    help="sealed report set, printed each epoch but NEVER used for selection")
    ap.add_argument("--select-on", default="dev", choices=("dev", "val"),
                    help="'val' reproduces the old, saturated-and-therefore-blind behaviour")
    ap.add_argument("--epochs", type=int, default=3)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--lr", type=float, default=3e-5, help="encoder fine-tuning lr")
    ap.add_argument("--head-lr", type=float, default=1e-3, help="freshly-initialised heads")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--device", default="auto")
    ap.add_argument("--limit", type=int, default=0, help="cap training rows (smoke runs)")
    ap.add_argument("--val-limit", type=int, default=800,
                    help="rows of val scored per epoch; e2e is one forward per row")
    ap.add_argument("--out", default=os.path.join(HERE, "artifacts_encoder"))
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    device = pick_device(args.device)

    rows_train = load_rows(os.path.abspath(args.train))
    rows_val = load_rows(os.path.abspath(args.val))
    rows_dev = load_rows(os.path.abspath(args.dev)) if os.path.exists(args.dev) else []
    rows_ood = load_rows(os.path.abspath(args.ood)) if os.path.exists(args.ood) else []
    if args.limit:
        rows_train = rows_train[:args.limit]
    rows_val_sel = rows_val[:args.val_limit] if args.val_limit else rows_val
    if args.select_on == "dev" and not rows_dev:
        sys.exit(f"no dev set at {args.dev} — run: python -m eval.make_dev_testset")

    intents, tags = build_label_maps(rows_train)
    os.makedirs(args.out, exist_ok=True)
    save_maps(os.path.join(args.out, "maps.json"), intents, tags, args.preset)
    id2intent = {v: k for k, v in intents.items()}
    id2tag = {v: k for k, v in tags.items()}

    tokenizer = load_tokenizer(args.preset)
    model = EncoderIntentSlotNet(args.preset, len(intents), len(tags), tokenizer=tokenizer)
    model.to(device)
    n_params = model.num_params()
    print(f"{PRESETS.get(args.preset, args.preset)} on {device} | params={n_params:,} "
          f"({n_params * 4 / 1e6:.0f} MB fp32, ~{n_params / 1e6:.0f} MB int8)")
    print(f"intents={len(intents)} tags={len(tags)} train={len(rows_train)} "
          f"val={len(rows_val_sel)}/{len(rows_val)} dev={len(rows_dev)} "
          f"ood={len(rows_ood)} | selecting on {args.select_on}")

    dl = DataLoader(EncoderCmdDataset(rows_train, tokenizer, intents, tags),
                    batch_size=args.batch, shuffle=True)
    # Pretrained weights want a small lr; the two heads are random, so they get
    # their own much larger one.
    head_params = list(model.slot_head.parameters()) + list(model.intent_head.parameters())
    head_ids = {id(p) for p in head_params}
    opt = torch.optim.AdamW([
        {"params": [p for p in model.parameters() if id(p) not in head_ids], "lr": args.lr},
        {"params": head_params, "lr": args.head_lr},
    ], weight_decay=0.01)

    n_tags = len(tags)
    best, history = -1.0, []
    for epoch in range(1, args.epochs + 1):
        model.train()
        tot, t0 = 0.0, time.time()
        for step, (ids, attn, intent, labels) in enumerate(dl, 1):
            ids, attn = ids.to(device), attn.to(device)
            intent, labels = intent.to(device), labels.to(device)
            il, sl = model(ids, attn)
            loss = (F.cross_entropy(il, intent) +
                    F.cross_entropy(sl.reshape(-1, n_tags), labels.reshape(-1),
                                    ignore_index=IGNORE))
            opt.zero_grad()
            loss.backward()
            opt.step()
            tot += float(loss.detach())
            if step % 100 == 0:
                print(f"  epoch {epoch} step {step}/{len(dl)} loss={tot/step:.4f} "
                      f"({time.time()-t0:.0f}s)", flush=True)

        val = e2e_exact(model, rows_val_sel, tokenizer, id2intent, id2tag, device)
        dev = (e2e_exact(model, rows_dev, tokenizer, id2intent, id2tag, device)
               if rows_dev else 0.0)
        ood = (e2e_exact(model, rows_ood, tokenizer, id2intent, id2tag, device)
               if rows_ood else 0.0)
        history.append({"epoch": epoch, "loss": tot / len(dl),
                        "val": val, "dev": dev, "ood": ood})
        # Select on hand-authored held-out phrasing. Selecting on val saturates
        # at 100% and ranks checkpoints by nothing — it kept the worse one in
        # three of three runs (#1847). ood is printed but never selected on:
        # tuning against it would make it describe those rows instead of
        # predicting the next ones.
        score = dev if args.select_on == "dev" else val
        flag = ""
        if score > best:
            best = score
            torch.save({"state_dict": model.state_dict(),
                        "model_name": args.preset}, os.path.join(args.out, "model.pt"))
            flag = "  *saved"
        print(f"epoch {epoch:2d}  loss={tot/len(dl):.4f}  val={val:.1%}  "
              f"dev={dev:.1%}  ood={ood:.1%}  ({time.time()-t0:.0f}s){flag}", flush=True)

    json.dump({"preset": args.preset, "params": n_params,
               "select_on": args.select_on, "history": history},
              open(os.path.join(args.out, "history.json"), "w"), indent=2)
    kept = max(history, key=lambda h: h[args.select_on])
    print(f"\nkept epoch {kept['epoch']} on {args.select_on}={best:.1%} "
          f"-> ood={kept['ood']:.1%}  | params={n_params:,}")


if __name__ == "__main__":
    main()
