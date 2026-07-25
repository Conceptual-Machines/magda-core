# Router model — findings

## Where it landed

**Core** = the 81 held-out cases whose label follows from a named operation or
object. That is the fast inference surface and the metric. The other 8 are
tagged `fuzzy` and scored separately (see below).

| | val (same templates) | held-out core |
|---|---|---|
| v0 templates only | 100.0% | 59.3% |
| v1 + synonym banks | 100.0% | 72.8% |
| v2 + shared section vocab | 100.0% | 81.5% |
| v3 + LLM teacher paraphrases | 100.0% | **88.9%** |

v3: 116,491 params, ~455 KB of float32, 0.27 ms/request in torch. Per language:
en 92.3%, ja 78.6%, ru 78.6%, zh 100.0%.

The model roughly doubled (64k -> 116k params) purely because the vocabulary
went 1,419 -> 3,049: the teacher introduced words no template author wrote,
which is the entire point. The conv trunk did not change.

(v2 and v3 are measured. The v0/v1 core figures are *derived* from those runs'
recorded misroute lists — 3 and 4 of the 8 fuzzy cases failed respectively —
because the core/fuzzy split postdates them and their checkpoints were
overwritten.)

Fuzzy cases score 3/8, and that is fine — they are the LLM path's job by
construction, not a router defect to engineer against.

The number that matters most is not the headline: **MUSIC -> COMMAND is 0** in
every version. That is the one confusion with a destructive outcome — a music
request scored against the command model gets mangled into DSL. MUSIC's actual
leakage is to BOTH (which still runs the music agent) and to MIXING/DRUM, which
degrade to a wrong answer rather than a wrong edit.

## Do not score fuzzy requests against this model

The first cut of this file reported a single 77.5% over all 89 cases and framed
the failures as a data-coverage problem to be solved with an LLM teacher. That
was the wrong frame, and it came from a test set that mixed two populations.

Fast inference is *deterministic classification over a fixed label set*. A
request like "the beat is too stiff, loosen it up" or "i need four chords that
feel like sunday morning" has no named operation in it — recovering the agent
means interpreting an aesthetic judgement, which is what the LLM path is for.
Counting those as router failures inflates the apparent gap and points the work
in the wrong direction.

Splitting them out moved the metric 77.5% -> 81.5%. Worth being clear that this
is a **4-point reframe, not a rescue**: the remaining 15 core failures are real
vocabulary gaps, not misfiled fuzz.

## The template-overfit trap (v0 -> v1)

First trained model: **100.0% on the val split, 59.6% on the held-out set.**

That gap is the whole story. The val split is drawn from the same template bank
as training, so it measures memorisation of ~120 phrasings, not routing. The
hand-authored held-out set uses wordings the templates never contain, and there
the model collapsed: unknown words arrive as `<UNK>`, and once a request is
mostly `<UNK>` the classifier is guessing from sentence length.

Representative v0 misroutes:

```
'get rid of track 3'            gold=COMMAND    got=SESSION
'chuck an eq on the vocal chain' gold=COMMAND   got=AUTOMATION
'wipe the curves off the pad track' gold=AUTOMATION got=COMMAND
'the vocal keeps disappearing behind the synths' gold=MIXING got=COMMAND
```

None of these are the "fuzzy one-off" the issue accepts. `get rid of track 3`
is a bread-and-butter command. The failure was vocabulary coverage, not
ambiguity.

**Never report the val number for this model.** It is a training-progress
signal only. The held-out set is the metric.

## What fixed it: synonym alternation, not capacity

Capacity was never the constraint — 100% val on 58k params means the model can
already separate the classes it has seen. The fix was diversity on the input
side:

1. `router/synonyms.py` — per-language banks of domain synonyms, applied to the
   template's literal text before slot substitution. One template becomes dozens
   of phrasings and the vocabulary grows to cover words producers actually type.
2. Wider slot pools and more phrasings for the low-arity classes.
3. Word dropout (`--drop 0.15`) so the model learns to classify from the words
   it does know rather than assuming full coverage.

## Shared vocabulary must be generated in every class that can use it

The v1 confusion matrix exposed a data-design bug, not a model weakness:

```
'ideas for the pre chorus harmony'   gold=MUSIC  got=SESSION
```

`chorus` existed in exactly one pool — SESSION's `scene` names ("launch the
chorus scene"). So the model learned `chorus -> SESSION`, which is correct
inside the generated distribution and nonsense outside it. Song sections are
ordinary musical vocabulary that occurs in every class.

The fix is a shared `section` pool used by MUSIC, BOTH, DRUM, AUTOMATION and
MIXING as well as SESSION. The general rule this exposes: **any word that can
legitimately appear in several classes has to be *generated* in several
classes**, or synthetic data turns it into a spurious class marker. Worth
checking for the next word that shows up in one pool only.

## Class balance is not optional here

MIXING and SESSION are *questions*, not parameterized commands — most of their
phrasings carry no slots, so a naive per-template cap starved them (66 rows vs
BOTH's 700). Two changes:

- `capacity()` counts filler and synonym alternatives, not just slot
  combinations, so a slot-free template is not capped at one row.
- Training uses inverse-frequency class weights. Without them the loss is
  dominated by COMMAND (which also absorbs the command-model corpus) and a
  router that never predicts SESSION still scores well on raw accuracy.

## Honesty note on the test set

The seed and synonym banks were authored *after* seeing which held-out cases
v0 failed. The entries are ordinary domain vocabulary rather than the specific
test strings, but the number is nonetheless somewhat optimistic — this is now
closer to a development set than a blind one. A genuinely blind evaluation
needs phrasings from real user logs, which do not exist yet.

What is *not* optimistic: `tests/test_router_model.cpp` locks the C++ backend to
the Python float reference case-by-case, so the shipped behaviour is exactly the
measured behaviour whatever its accuracy.

## Tokenizer: why the command model's could not be reused

`dataset/tagging.tokenize` is ASCII-only, so:

```python
>>> tokenize("ベーストラックを作成")
[]
>>> tokenize("создай басовую дорожку")
[]
```

The command model's shipped vocab has **zero** non-ASCII tokens; its 45 ja/ru/zh
seed rows train against empty sequences. It is English-only in practice despite
the multilingual intent in its README.

For the command model that is a latent gap. For the router it would be a live
bug: the router runs on every console turn, so every non-Latin message would
route to the view default regardless of content. Hence `router/text.py`.

Note for a future multilingual command model: fixing its tokenizer is necessary
but **not sufficient**. `dataset.tagging._tag_span` locates slots by
substring-matching the canonical *English* value against the token stream, so
`"Bass"` is unfindable in `ベーストラックを作成` even after tokenization works.
That needs a per-language lexicon threaded through the tagger and the
reconstructor.

## What is left

**Verify the paraphrases or do not use them.** The teacher pass (v3) added 7,542
rewrites and took core accuracy 81.5% -> 88.9%, but the headline number hides
the important detail: **8% of proposed rewrites drifted intent** and were thrown
away by the second pass. A paraphraser asked to reword "suggest jazz chords"
happily returns "add some jazz chords to a new track" — still a sensible
request, now the wrong label. At the 3x repeat weight teacher rows carry, those
~670 mislabeled rows would have done real damage. The two-pass design (rewrite,
then independently re-label at temperature 0 and keep only agreements) is not
optional polish; it is what makes the pass safe.

Cost was 2,800 API calls on `deepseek-v4-flash`. Note `deepseek-chat` is no
longer a valid model name — the command POC's `dataset/teacher.py` still
defaults to it and will 400.

Where v3 still loses: ja and ru sit at 78.6% while en is 92.3% and zh is 100%.
The teacher produced an even ~1,880 rows per language, so this is not a volume
gap. Worth looking at whether the ja/ru held-out cases lean harder on idiom than
the en/zh ones do — with 14 cases per language, it is also within noise, which
is itself a reason to grow the test set before tuning against it.

Two smaller levers, still untried:

- **Subword fallback.** OOV tokens currently all collapse to one `<UNK>`, so
  three unknown words are indistinguishable. Hashing character trigrams into a
  fixed bucket range (FastText-style, with an FNV-1a that both sides mirror)
  would make unknown words partially informative and share signal across
  morphological variants. Portable to C++, ~40 lines each side.
- **Real traffic.** Every number here comes from data written by the same author
  as the test set. The first genuinely blind evaluation will come from console
  logs, and should replace `eval/testset.jsonl` as the metric when it exists.
