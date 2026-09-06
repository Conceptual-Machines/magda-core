---
name: doxygen-comments
description: Doxygen comment form for C++. Load before writing or editing a comment on a class, struct, enum, function, method, or file header. Those get a /** @brief */ block; fields and lines inside a body get /// or //.
---

# Doxygen, Not Multiline

What decides the form is **what is being documented**, not how long the comment is.

- A **class, struct, enum, function or method** gets a `/** */` block with `@brief`.
- **Anything else** -- a field, a constant, an enumerator, a line inside a body -- gets `///` or `//`.

A run of `//` or `///` lines above a class or a function is the wrong form whatever it says.
This is about the form a comment takes, not about what has to carry one: a declaration that needs
no comment gets none.

Length is `comments-not-essays`' job and still applies: `@brief` is one sentence, and the body
under it is a short paragraph at most. A block that has nothing to add after the brief stays on
one line.

## The forms

One sentence, which is most of them:

    /** @brief Room for a window's MIDI, in events. */
    static int defaultMidiCapacity(const CaptureWindow& window);

A second thought after the brief:

    /**
     * @brief Rounds a block to @p bits, dithering on the way.
     *
     * Stateful on purpose: the shaper's feedback is a filter over the samples
     * before it. One per channel-set per render, reset between renders.
     */
    PcmQuantiser(int bits, int channels, DitherMode mode);

Not a function, not a class:

    /// Seconds, so reading at another rate keeps the delay the pass had.
    double roundTripSeconds = 0.0;

Never:

- a run of `//` or `///` lines above a class or a function
- `/* */` with one star (Doxygen ignores it)
- `\brief`, `\param` -- this repo is `@`, with no exceptions

## The tag vocabulary

`@file`, `@brief`, `@param`, `@p`, `@return`, `@ref`. That is the whole set in use across
magda/ (3061 `@brief`, 849 `@param`, 713 `@p`, 244 `@return`, 107 `@ref`, 87 `@file`).

`@note`, `@warning`, `@see`, `@pre`, `@throws`, `@code` appear zero or once. Do not introduce
them: a note is a sentence, and a warning is either a sentence or a reason the code should
change.

## @param and @return

Only when the name does not already carry it. `@param bitDepth The bit depth.` is noise; delete
the tag rather than write it.

Referring to a parameter in prose is `@p name`:

    /** @brief Round @p buffer's first @p numSamples onto the target's grid. */

## Where the block lives

On the declaration, once.

- Anything a header declares: the block is in the header. The definition in the `.cpp` repeats
  nothing; notes inside the body are plain `//` lines about mechanics.
- A file-local helper (anonymous namespace, `static`): the block sits on the definition, since
  that is the only declaration there is. It is still a function, so it still gets a block.

## File headers

Every engine and model file opens with one:

    /**
     * @file AudioFileSink.hpp
     * @brief What turns a render into a file (#2447).
     *
     * A paragraph on what the unit is for, or nothing.
     */

`@file` then `@brief`, and the brief is a phrase, not a sentence about the class list.

## Enums

The enum is a type, so it gets a block. Its enumerators are not, so they get `///`:

    /** @brief What is added before the round. */
    enum class DitherMode : std::uint8_t {
        /// Round and nothing else. For a target with no quantisation to hide.
        none,

        /// Two uniform randoms summed, scaled to one LSB. The default wherever
        /// the target is fixed point.
        tpdf,
    };

## When you touch old code

A `//` run on a class or a function gets converted while you are there. It is a form fix, not a
rewrite: keep the words, change the wrapper, cut anything the length rule forbids.

The reverse is never right. Do not flatten a `/** @brief */` block into `///` lines to lower the
number `scripts/comment_ratio.py` reports -- the ratio is a proxy for prose, and the block form
is not what makes a file wordy.
