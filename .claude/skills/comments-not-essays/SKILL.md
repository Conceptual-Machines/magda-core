---
name: comments-not-essays
description: Comment and docstring discipline. Load before writing or editing any comment, doc block, or docstring in code. Cuts essay-length doc blocks, rhetorical structure, rejected-alternatives narration, and restated code.
---

# Comments Are Not Essays

A comment is a line or two. Two paragraphs is the hard ceiling. Anything that needs more than
that is a technical document: put it in `docs/` and leave a one-line pointer.

This applies to every comment form: `//`, `///`, `/** */`, docstrings, module headers, file
headers, test-case preambles.

## The test, before any comment ships

**Would someone about to change this code do the wrong thing without it?**

No -> cut it. That is the whole rule.

## Never

- **Markdown section headings inside a comment.** `## Why it is not a meter`, `## One word`,
  `## What no writes means`. A doc block with headings is a document that has been pasted into a
  source file. Move it to `docs/` or delete it.
- **Narrating rejected alternatives.** "The alternative is X, which would be wrong because Y."
  Nobody is proposing X. State what the code does; the reader does not need the road not taken.
- **Building a case.** No "which is the whole of it", "which is exactly why", "and that is the
  point", "for the reason that". These are argument connectives. A settled decision needs no
  argument, only a statement.
- **Restating the code below it.** If the line says `if (x == nullptr) return;`, do not write
  "returns early when x is null".
- **Worked examples and arithmetic.** "at 48 kHz and 123 bpm a beat is 23,414.634 samples, so a
  thousand retriggers finish 366 samples late". Put it in a test, where it can fail.
- **Repeating a rationale across files.** Say it once, at the definition. Other sites get a
  cross-reference, not a copy.
- **Essay openings.** "The whole reason X is Y", "Two things come apart here", "What decides".

## What earns more than a line

Only these, and only the sentence that carries them:

- an issue number (`(#2305)`)
- an external constraint: a Tracktion quirk, a JUCE contract, a wire-format rule, a platform bug
- a measured figure, with its units
- a non-obvious decision someone would otherwise undo

## Shape

Doc block on a method: `@brief` one sentence, then `@param`/`@return` if the name does not
already say it. Fields and short methods: one `///` line. Follow the surrounding file.

    /**
     * @brief Publish what this block left @p handle doing. Audio thread.
     *
     * Once per block, from the pass that advanced it, so the reading and the
     * audio it describes are the same block.
     */

That is the maximum for a normal method. Not a section, not an argument, not a comparison to
what the old engine did.

## When you catch yourself

If a comment you are writing has a second paragraph, stop and ask whether the first one already
did the job. It usually did. If it genuinely did not, the material is a `docs/` page.

Trimming your own comments in a later pass does not fix this. Do not write them.

## MAGDA specifics

`CLAUDE.md` at the repo root carries the same rule. `scripts/comment_ratio.py` measures the
line ratio, but the ratio is a proxy: dense one-liners at a high ratio are fine, and a short
essay at a low ratio is not. Judge the prose, not the count.
