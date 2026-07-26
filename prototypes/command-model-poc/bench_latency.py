"""Per-command latency probe (host-side model path: tokenize -> forward ->
reconstruct -> render). PyTorch/M1 CPU — the dev regime, NOT the FPGA target."""
import statistics
import time

import torch

from model.intent_slot_parser import build_parser

torch.set_num_threads(1)  # single-thread: closest to a deterministic per-call cost
parse = build_parser()

CMDS = [
    "create a bass track",
    "add @serum to the vocals",
    "set the master volume to -6 dB",
    "transpose the notes on the bass up 5 semitones",
    "select every clip longer than 4 bars on the pads",
    "quantize the drum notes to 16ths",
    "group tracks 1, 2 and 3",
    "set the groove to Basic 8th Swing with strength 0.5",
]
REPS = 200

# warmup (first calls pay lazy-init / allocator costs)
for _ in range(30):
    parse("create a bass track")

print(f"{'command':<48} {'min':>7} {'med':>7} {'p95':>7}  (ms)")
print("-" * 74)
allt = []
for c in CMDS:
    ts = []
    for _ in range(REPS):
        t0 = time.perf_counter()
        parse(c)
        ts.append((time.perf_counter() - t0) * 1000)
    ts.sort()
    p95 = ts[int(0.95 * len(ts)) - 1]
    allt += ts
    print(f"{c:<48} {ts[0]:>7.3f} {statistics.median(ts):>7.3f} {p95:>7.3f}")

allt.sort()
print("-" * 74)
print(f"{'OVERALL (all commands)':<48} {allt[0]:>7.3f} "
      f"{statistics.median(allt):>7.3f} {allt[int(0.95*len(allt))-1]:>7.3f}")
print(f"\nthreads=1, reps={REPS}/command, {len(CMDS)} commands, "
      f"PyTorch {torch.__version__} on CPU")
