"""Export the pretrained-encoder command model to ONNX for the C++ backend.

Sibling to `export_cpp.py`, which bakes the conv net's weights into C++ source.
That trick does not scale to 184M parameters, so this path ships an ONNX graph
loaded by the ONNX Runtime already vendored for CLAP (`third_party/onnxruntime`,
pinned to 1.26.0 in the root CMakeLists.txt).

Emits into --out (default model/artifacts_onnx/):

    command_model.onnx    encoder + intent head + slot head, one graph
    tokenizer.json        HF fast tokenizer, including the alias special tokens
    maps.json             intent + tag id maps (copied from the checkpoint)
    parity_cases.json     (input, expected_dsl) for every committed eval row

The C++ side must reproduce `encoder_parser.predict_dsl` exactly; see
HANDOFF_ONNX.md §3 for the step-by-step contract. This script verifies the
half it can: that ONNX Runtime and PyTorch agree, end to end, on every
committed case. If they disagree here, nothing downstream is worth debugging.

    python -m model.export_onnx --artifacts model/artifacts_encoder_deberta
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from dataset.tagging import reconstruct  # noqa: E402
from magda_dsl import dsl  # noqa: E402
from model.data_encoder import encode_text, load_maps  # noqa: E402
from model.net_encoder import EncoderIntentSlotNet, load_tokenizer  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POC = os.path.dirname(HERE)

# Every committed evaluation row becomes a parity case. Both sets are included
# deliberately: testset.jsonl covers the intent surface exhaustively, and
# ood_testset.jsonl covers the phrasing the model actually has to survive.
EVAL_SETS = ["eval/testset.jsonl", "eval/ood_testset.jsonl", "eval/dev_testset.jsonl"]


def load_model(artifacts):
    intents, tags, model_name = load_maps(os.path.join(artifacts, "maps.json"))
    tokenizer = load_tokenizer(model_name)
    model = EncoderIntentSlotNet(model_name, len(intents), len(tags), tokenizer=tokenizer)
    ckpt = torch.load(os.path.join(artifacts, "model.pt"), map_location="cpu")
    model.load_state_dict(ckpt["state_dict"])
    model.eval()
    return model, tokenizer, intents, tags, model_name


def export_onnx(model, tokenizer, out_path, opset):
    """Trace the graph with a short real command, not zeros.

    Sequence length is dynamic: commands are 8-20 subwords and padding every
    forward to MAX_LEN would waste most of the compute. Batch is dynamic too,
    though the app only ever sends one.
    """
    words, ids, attn, _ = encode_text("create a bass track with @serum", tokenizer)
    # Trim the padding off the trace input so the exporter cannot bake in 64.
    keep = int(attn.sum())
    ids, attn = ids[:, :keep], attn[:, :keep]

    torch.onnx.export(
        model,
        (ids, attn),
        out_path,
        input_names=["input_ids", "attention_mask"],
        output_names=["intent_logits", "slot_logits"],
        dynamic_axes={
            "input_ids": {0: "batch", 1: "seq"},
            "attention_mask": {0: "batch", 1: "seq"},
            "intent_logits": {0: "batch"},
            "slot_logits": {0: "batch", 1: "seq"},
        },
        opset_version=opset,
        do_constant_folding=True,
        dynamo=False,
    )


def quantize(fp32_path, int8_path):
    """Dynamic int8 quantization of the weights (activations stay float).

    ~4x smaller with no calibration set needed. It is NOT free accuracy-wise —
    the conv-net port found float32 beat its quantized build (102/102 vs
    101/102, #1827) — so the caller re-scores rather than assuming.
    """
    from onnxruntime.quantization import QuantType, quantize_dynamic
    quantize_dynamic(fp32_path, int8_path, weight_type=QuantType.QInt8)


def onnx_predict(session, text, tokenizer, id2intent, id2tag):
    """The reference C++ pipeline, in Python, through ONNX Runtime."""
    words, ids, attn, first = encode_text(text, tokenizer)
    if not words:
        return ""
    keep = int(attn.sum())
    intent_logits, slot_logits = session.run(
        None,
        {"input_ids": ids[:, :keep].numpy().astype(np.int64),
         "attention_mask": attn[:, :keep].numpy().astype(np.int64)},
    )
    intent = id2intent[int(intent_logits[0].argmax())]
    tag_ids = slot_logits[0].argmax(-1)
    tags = [id2tag[int(tag_ids[p])] if 0 <= p < keep else "O" for p in first]
    try:
        return dsl.render(reconstruct(intent, words, tags))
    except Exception:
        return ""


def torch_predict(model, text, tokenizer, id2intent, id2tag):
    from model.encoder_parser import predict_dsl
    return predict_dsl(model, text, tokenizer, id2intent, id2tag, "cpu")


def load_eval_rows():
    rows = []
    for rel in EVAL_SETS:
        path = os.path.join(POC, rel)
        if not os.path.exists(path):
            print(f"  skip {rel} (missing)")
            continue
        n = 0
        for line in open(path, encoding="utf-8"):
            if line.strip():
                r = json.loads(line)
                rows.append({"set": os.path.basename(rel), "input": r["input"],
                             "gold": r["output"]})
                n += 1
        print(f"  {rel}: {n}")
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--artifacts", default=os.path.join(HERE, "artifacts_encoder_deberta"))
    ap.add_argument("--out", default=os.path.join(HERE, "artifacts_onnx"))
    ap.add_argument("--opset", type=int, default=17)
    ap.add_argument("--no-quantize", action="store_true",
                    help="skip the int8 build (fp32 is ~736 MB, int8 ~185 MB)")
    args = ap.parse_args()

    artifacts = os.path.abspath(args.artifacts)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    model, tokenizer, intents, tags, model_name = load_model(artifacts)
    id2intent = {v: k for k, v in intents.items()}
    id2tag = {v: k for k, v in tags.items()}
    n_params = model.num_params()
    print(f"loaded {model_name}: {n_params:,} params, "
          f"{len(intents)} intents, {len(tags)} tags")

    onnx_path = os.path.join(out, "command_model.onnx")
    export_onnx(model, tokenizer, onnx_path, args.opset)
    size_mb = os.path.getsize(onnx_path) / 1e6
    print(f"onnx: {onnx_path} ({size_mb:.0f} MB, opset {args.opset})")

    # Tokenizer: save the fast tokenizer's JSON directly. It carries the alias
    # special tokens added in load_tokenizer, which the C++ tokenizer needs or
    # "<alias>" splits into "<", "alias", ">" and every plugin slot breaks.
    tok_path = os.path.join(out, "tokenizer.json")
    tokenizer.backend_tokenizer.save(tok_path)
    for t in ("<alias>", "<alias.param>"):
        assert tokenizer.convert_tokens_to_ids(t) != tokenizer.unk_token_id, \
            f"{t} is not in the exported tokenizer"
    print(f"tokenizer: {tok_path} (vocab {len(tokenizer)}, alias tokens present)")

    shutil.copy(os.path.join(artifacts, "maps.json"), os.path.join(out, "maps.json"))

    int8_path = os.path.join(out, "command_model.int8.onnx")
    if not args.no_quantize:
        quantize(onnx_path, int8_path)
        print(f"int8: {int8_path} ({os.path.getsize(int8_path) / 1e6:.0f} MB)")

    # ---- verify ORT == PyTorch on every committed case --------------------
    import onnxruntime as ort
    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    sess_i8 = (ort.InferenceSession(int8_path, providers=["CPUExecutionProvider"])
               if os.path.exists(int8_path) else None)

    print("\nparity cases:")
    rows = load_eval_rows()

    cases, mismatches, correct, correct_i8, drift_i8 = [], 0, 0, 0, 0
    for r in rows:
        got_onnx = onnx_predict(sess, r["input"], tokenizer, id2intent, id2tag)
        got_torch = torch_predict(model, r["input"], tokenizer, id2intent, id2tag)
        if dsl.normalize(got_onnx) != dsl.normalize(got_torch):
            mismatches += 1
            print(f"  ONNX != torch: {r['input']!r}\n"
                  f"    onnx : {got_onnx!r}\n    torch: {got_torch!r}")
        correct += dsl.normalize(got_onnx) == dsl.normalize(r["gold"])
        if sess_i8 is not None:
            got_i8 = onnx_predict(sess_i8, r["input"], tokenizer, id2intent, id2tag)
            correct_i8 += dsl.normalize(got_i8) == dsl.normalize(r["gold"])
            drift_i8 += dsl.normalize(got_i8) != dsl.normalize(got_onnx)
        # The fixture locks C++ to what the MODEL produces, not to gold — a
        # parity test must fail on divergence, not on the model being wrong.
        cases.append({"set": r["set"], "input": r["input"], "expected": got_onnx})

    with open(os.path.join(out, "parity_cases.json"), "w", encoding="utf-8") as f:
        json.dump(cases, f, ensure_ascii=False, indent=1)

    n = max(len(rows), 1)
    print(f"\nONNX vs PyTorch: {len(rows) - mismatches}/{len(rows)} identical")
    print(f"fp32 vs gold:    {correct}/{len(rows)} exact ({correct / n:.1%})")
    if sess_i8 is not None:
        print(f"int8 vs gold:    {correct_i8}/{len(rows)} exact ({correct_i8 / n:.1%})"
              f"   [{drift_i8} outputs differ from fp32]")
    print(f"wrote {len(cases)} parity cases -> {out}/parity_cases.json")
    if mismatches:
        sys.exit(f"ERROR: {mismatches} ONNX/PyTorch mismatches — do not ship this graph")


if __name__ == "__main__":
    main()
