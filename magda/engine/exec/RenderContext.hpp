#pragma once

#include <cstdint>

/**
 * @file RenderContext.hpp
 * @brief What the executor renders into, and what it is asked to render now.
 */

namespace magda::engine {

/**
 * @brief Fixed properties of the audio device a plan is prepared for.
 *
 * Settled before any block runs and constant until the next prepare, so
 * everything sized from it (scratch buffers, device state) is allocated once
 * off the audio thread.
 */
struct RenderContext {
    double sampleRate = 44100.0;

    /// Largest block the executor will be asked for. Blocks may be shorter;
    /// output is identical either way, because nothing here quantises to the
    /// block (block size is an I/O batching concept, never a precision one).
    int maxBlockSize = 512;

    /// Channels on every audio port. Stereo throughout: the model has no mono
    /// tracks and the plan carries no channel layouts.
    int numChannels = 2;

    bool operator==(const RenderContext&) const = default;
};

/**
 * @brief The transport state for one block.
 *
 * Samples appear here and nowhere else in the engine. Beats are the canonical
 * musical domain; the executor's sample clock is the one place a musical
 * position becomes a sample position. There is no tempo map yet, so the caller
 * supplies the cursor directly and the transport slice replaces it with a
 * tempo-snapshot cursor reading beats.
 */
struct BlockInfo {
    int numSamples = 0;
    std::int64_t timelineSample = 0;
    bool playing = true;
};

}  // namespace magda::engine
