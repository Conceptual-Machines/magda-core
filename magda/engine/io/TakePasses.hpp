#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "exec/RenderContext.hpp"
#include "transport/TransportState.hpp"

/**
 * @file TakePasses.hpp
 * @brief The loop-record rules a take keeps, whatever it is made of.
 *
 * Audio takes are files and MIDI takes are event lists, but which pass plays
 * and what counts as a pass boundary are the model's questions and have one
 * answer each (#2461, #2462).
 */

namespace magda::engine {

/// A wrap anchors the cursor exactly on the loop start, so anything further off
/// than this is a locate rather than a pass boundary.
constexpr double kLoopStartTolerance = 1.0e-6;

/// How short the final pass has to be to count as a stop rather than a take.
constexpr double kFullPassFraction = 0.95;

inline bool atLoopStart(const BlockInfo& block, const LoopRange& loop) {
    return loop.valid() && std::abs(block.beats.start - loop.startBeat) <= kLoopStartTolerance;
}

/**
 * @brief The last full pass, stepping back one if the final pass was cut short.
 *
 * Only the last pass can be cut short, and only the first can run long: a
 * negative adjustment pads its head, and @p headPadding is that padding when
 * the take's own first pass is still here to carry it. Discounted rather than
 * measured, so a pass is judged against what was played rather than against a
 * correction at the top of the file.
 *
 * Measured in samples rather than in how much was played into it, because what
 * makes the final pass different is a stop landing in the middle of it, which
 * is a fact about its length and not about its contents.
 */
inline std::size_t activeTake(std::span<const std::int64_t> lengths, std::int64_t headPadding = 0) {
    const auto played = [&](std::size_t pass) {
        return lengths[pass] - (pass == 0 ? headPadding : 0);
    };

    std::int64_t longest = 0;
    for (std::size_t pass = 0; pass < lengths.size(); ++pass)
        longest = std::max(longest, played(pass));

    const auto last = lengths.size() - 1;
    if (lengths.size() > 1 &&
        static_cast<double>(played(last)) < static_cast<double>(longest) * kFullPassFraction)
        return last - 1;

    return last;
}

}  // namespace magda::engine
