"""Run a parser/model against the fixed test set and print POC metrics.

    python -m eval.run                 # baseline rule parser
    python -m eval.run --show-fails    # also dump mismatches

Parsers are pluggable. Today: 'baseline' (rule_parser). A trained model is
added later by wrapping its inference call as a `str -> DSL str` function and
registering it here.
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from baseline import rule_parser  # noqa: E402
from eval import metrics  # noqa: E402

PARSERS = {
    "baseline": rule_parser.parse,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--parser", default="baseline", choices=sorted(PARSERS))
    ap.add_argument("--model", help="path to a trained .gguf; overrides --parser")
    ap.add_argument("--torch-model", nargs="?", const="model/artifacts",
                    help="dir with the Brevitas intent+slots checkpoint (default model/artifacts)")
    ap.add_argument("--testset", default=os.path.join(os.path.dirname(__file__), "testset.jsonl"))
    ap.add_argument("--show-fails", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.testset):
        sys.exit(f"test set not found: {args.testset}\nrun: python -m eval.make_testset")

    rows = metrics.load_testset(args.testset)
    if args.model:
        from model.gguf_parser import make_parser
        parser_fn = make_parser(args.model)
        args.parser = os.path.basename(args.model)
    elif args.torch_model:
        from model.intent_slot_parser import build_parser
        parser_fn = build_parser(os.path.abspath(args.torch_model))
        args.parser = "intent+slots"
    else:
        parser_fn = PARSERS[args.parser]
    overall, per_lang, records = metrics.evaluate(rows, parser_fn)

    print(metrics.format_report(overall, per_lang, title=f"{args.parser} vs {os.path.basename(args.testset)}"))

    # Targets from the POC spec (adapted: valid-JSON -> valid-syntax).
    print("\n  targets: syntax>=95%  command>=90%  exact>=80%  latency<500ms")

    if args.show_fails:
        fails = [r for r in records if not r["exact"]]
        print(f"\n-- {len(fails)} non-exact ({len([r for r in fails if not r['valid']])} also invalid) --")
        for r in fails:
            tag = "INVALID" if not r["valid"] else "wrong"
            print(f"[{r['lang']}/{tag}] {r['input']}")
            print(f"   gold: {r['gold']!r}")
            print(f"   pred: {r['pred']!r}")


if __name__ == "__main__":
    main()
