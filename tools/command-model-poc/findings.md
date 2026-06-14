# Spike: MAGDA tiny command model -> FPGA

Status as of 2026-06-13. Where this stands and why, so the next session doesn't
re-litigate decisions. This `command-model-poc/` folder is the spike; the code
and this doc live here.

## Goal (as it actually evolved)

Natural-language DAW automation ("create a bass track with serum and ott") ->
validated MAGDA command DSL, executed through the EXISTING apply/undo pipeline.
The ultimate target is **running inference on an FPGA** (not just local/offline)
- that constraint was not in the original spec and reshaped the whole approach.

Out of scope (handled by other agents): music generation, mixing, per-parameter
DSP, creative decisions.

## Locked decisions (with rationale)

1. **Emit the EXISTING DSL, not new JSON.** The shipping `CommandAgent` already
   produces a functional chained DSL (`magda/agents/dsl_grammar.hpp`), CFG-
   constrained, wired to `InstructionExecutor` + `UndoManager`. We target that
   grammar so anything produced is already executable. (Most of the spec's
   "apply/preview/undo" already exists in the repo.)

2. **English-only.** Multilingual was explored (en/ja/ru/zh) then dropped for the
   FPGA target: a multilingual tokenizer's embedding table (~150k vocab) is the
   thing that does NOT fit on-chip. English collapses the vocab to ~100 tokens.
   Multilingual code is PARKED behind flags (`i18n.py`, `--langs`,
   `INCLUDE_NON_EN`), not deleted.

3. **Intent + slot tagging, NOT generative seq2seq.** This is the pivotal call.
   A generative model emits DSL token-by-token = an autoregressive loop with KV
   cache + sampling + feedback - exactly what FINN/hls4ml/FPGA dataflow do NOT
   do. Because the instruction set is FIXED (closed command set), we don't need
   generation: classify the intent + tag each input word with its slot role in a
   SINGLE forward pass. A deterministic reconstructor (`dataset/tagging.py:
   reconstruct`) + `dsl.render` rebuild the exact DSL on the CPU side. The net
   never emits DSL -> cannot produce malformed DSL (correctness guarantee) AND
   streams cleanly on FPGA.

4. **Values are generic spans, not understood.** Names, plugins, colours, ids,
   pan words, and numeric values are tagged as spans; the model is blind to
   their content. The ARM parses "-6 dB" -> `volume_db=-6`, or "hard right" ->
   `pan=1`. The parameter identity comes from the INTENT, so a single generic
   VALUE slot suffices for single-value commands. Only MULTI-value utterances
   need role-typed value slots.

## What's built and working

Pipeline (all local, M1):
- `magda_dsl/` - grammar (mirrored from dsl_grammar.hpp), canonical renderer,
  vocab (plugins/colours), lark validator. All gold DSL parses under the real CFG.
- `dataset/generate.py` - template synthetic data, leakage-guarded train/val
  split (train 12000 / val 1500 / test 72, zero overlap).
- `dataset/tagging.py` - (input, actions) -> (intent, BIO tags); round-trip
  reconstruct -> render == gold = **95.5%** (the ~4.5% is cosmetic name casing,
  e.g. "FX" vs "Fx").
- `baseline/rule_parser.py` - Phase-1 no-model floor: 100% on the English test set
  (but it's hand-fit regex; doesn't generalize - that's the point of the model).
- `model/` - Brevitas QAT intent+slots model + training + eval parser.

### Model result (first untuned run)

| metric | value |
|---|---|
| params | **43,782** |
| size @ 4-bit weights | **~21.4 KB** |
| val end-to-end exact | 96.7% |
| **held-out test exact** | **100.0%** (syntax 100%, command 100%) |
| latency | 2.09 ms (CPU/M1 PyTorch; FPGA will be ~us, deterministic) |

21.4 KB is trivially on-chip on the KV260 (MB of BRAM/URAM) - streaming from DRAM
never enters the picture. This validates the FPGA thesis in software before the
board ships.

Architecture (`model/net.py`): embedding -> 3 dilated 1D-conv blocks (QuantConv1d
+ QuantReLU, RF ~15 tokens) -> intent head (masked mean-pool) + slot head
(per-token). Convs chosen because FINN streams them natively; no recurrence/
attention. OOV-name augmentation (`unk_aug`) forces tagging by context so unseen
track names generalize.

The fixed English test set currently has no misses after the volume/pan,
clip-selection, and select-then-rename refresh. Remaining risk is generalisation
outside the template distribution, especially more free-form numeric,
stereo-field, clip-selection, and chained-operation phrasing. Numbered rename
templates like `{i}` need tokenizer support before they should be fixed-eval
targets.

## Hardware decision

- **Board: AMD Kria KV260 Vision AI Starter Kit (DigiKey SK-KV260-G).** Zynq
  UltraScale+ SoC: quad A53 (runs tokenizer + reconstruct + render + I/O via
  Kria-PYNQ) + fabric (runs the net via FINN) + URAM (on-chip headroom).
  Also a productizable SOM (K26). Pynq-Z2 was the budget alternative.
- **Still to buy:** microSD (16GB+, boots Kria-PYNQ), Ethernet cable, USB cable.
  PSU is included in the kit.
- **Build host:** the user's x86 Linux box (x86-64, 16GB RAM, ~150GB disk -
  confirmed). Runs Vivado/Vitis + FINN-in-Docker. The M1 CANNOT build (Xilinx
  tools are x86-Linux only); M1 stays the eval/latency + dev machine.
- **Gotcha:** pin the Vivado version to the chosen FINN release.

## Training infra (the GGUF teacher path, parallel track)

The generative 0.5B path is NOT the FPGA artifact but stays as the distillation
teacher / accuracy ceiling:
- `model/train_colab.ipynb` - unsloth LoRA on Qwen2.5-0.5B-Instruct, exports GGUF.
- Data staged to Google Drive (`My Drive/magda-command-model/`) for Colab; GGUF
  syncs back via Drive-for-Desktop.
- `eval.run --model X.gguf` scores it on the M1 (llama-cpp-python).
- Qwen is NOT load-bearing: data gen is deterministic templates; an LLM teacher
  is only an optional v2 lever for input paraphrase diversity, and is pluggable.

## Next steps

1. **Tune** the intent+slots model beyond the template set: more epochs, sweep
   `unk_aug` 0.15-0.2, and add harder free-form held-out phrasing. Push weights
   toward ternary (`--wbits 2`/1.58) and watch the accuracy/size trade in
   `eval.run`.
2. **Expand the instruction set** - cheap on the model side (a few output
   classes). Real cost is: (a) extend the DSL grammar/interpreter for the gaps
   (`duplicate_track`, `remove_plugin`, `route_track`) so they're executable;
   (b) templates + tagger updates; (c) numeric/relational slots. Stay in the
   STRUCTURAL-automation domain to preserve the single-pass framing.
3. **FINN export hardening** (on the Linux box once Vivado is in): thread
   QuantTensors end-to-end, move the embedding to a host lookup / on-chip ROM,
   ONNX -> FINN dataflow build.
4. **Deploy** to KV260: FINN bitstream on fabric, PYNQ glue on the A53.

## How to reproduce the current state

```bash
cd tools/command-model-poc
pip install -r requirements.txt          # lark; + torch, brevitas for the model
python3 -m eval.make_testset             # fixed test set (72, en)
python3 -m dataset.generate --n 12000 --val 1500 # frozen splits
python3 -m dataset.tagging --demo        # see tags + 95.5% round-trip
python3 -m model.train_intent_slots --epochs 25  # -> model/artifacts/ (~21.4KB)
python3 -m eval.run --torch-model        # 100.0% test exact
python3 -m eval.run                      # rule baseline for comparison
```
