# MAGDA Core

## Comment density

A comment is a line or two. Longer needs a reason: an issue number, an external constraint
(a TE quirk, a JUCE contract, a wire-format rule), a measured figure, or a non-obvious decision.
Don't restate the code below it, narrate rejected alternatives, or write worked examples —
those belong in `docs/` or a test.

Check the ratio with `scripts/comment_ratio.py`; `--check <percent>` fails on any file over
the threshold.
