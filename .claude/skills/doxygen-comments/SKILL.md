---
name: doxygen-comments
description: Doxygen comment form for C++. Load before writing or editing a comment on a class, struct, enum, method, free function, or file header. Anything longer than one line on those is a /** @brief */ block, never a run of // or /// lines.
---

# Doxygen, Not Multiline

A comment on a class, struct, enum, method or free function that runs to more than one line is
a Doxygen block. A stack of `//` or `///` lines above a declaration is the wrong form, whatever
it says.

This is about form only. `comments-not-essays` governs length and still applies: `@brief` is one
sentence and the body is a short paragraph at most.

## The two forms

One line, on a field, an enumerator or a method whose name nearly says it:

    /// One least significant bit of the target, as a float amplitude.
    float lsb() const;

More than one line, anywhere:

    /**
     * @brief Rounds a block to @p bits, dithering on the way.
     *
     * Stateful on purpose: the shaper's feedback is a filter over the samples
     * before it. One per channel-set per render, reset between renders.
     */
    PcmQuantiser(int bits, int channels, DitherMode mode);

Never:

- two or more `//` or `///` lines stacked above a declaration
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

    /// Round @p buffer's first @p numSamples in place onto the target's grid.

## Where the block lives

On the declaration, once.

- Anything a header declares: the block is in the header. The definition in the `.cpp` repeats
  nothing; notes inside the body are plain `//` lines about mechanics.
- A file-local helper (anonymous namespace, `static`): the block sits on the definition, since
  that is the only declaration there is.

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

The enum gets a block or a `///`; each enumerator gets whatever it needs, in the same two forms:

    enum class DitherMode : std::uint8_t {
        /// Round and nothing else. For a target with no quantisation to hide.
        none,

        /**
         * @brief Two uniform randoms summed, scaled to one LSB.
         *
         * The default wherever the target is fixed point: a triangular
         * distribution is what makes the error independent of the signal.
         */
        tpdf,
    };

## When you touch old code

A multi-line `//` run on a class or method gets converted while you are there. It is a form fix,
not a rewrite: keep the words, change the wrapper, cut anything the length rule forbids.

The reverse is never right. Do not flatten a `/** @brief */` block into `///` lines to lower the
number `scripts/comment_ratio.py` reports -- the ratio is a proxy for prose, and the doc block
form is not what makes a file wordy.
