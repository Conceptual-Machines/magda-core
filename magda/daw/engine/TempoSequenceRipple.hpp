#pragma once

/**
 * @file TempoSequenceRipple.hpp
 * @brief Building the edit-wide tempo ripple, without naming an engine (#2757).
 */

#include <functional>
#include <memory>

#include "../core/TimeTypes.hpp"
#include "../core/UndoManager.hpp"

namespace magda {

enum class TempoSequenceRippleMode {
    Insert,
    Delete,
    Duplicate,
};

/**
 * @brief Builds the ripple for the range @p start to @p end, or nothing.
 *
 * The sequences live in the Edit, which only the fork has, so it registers this and the
 * native engine ripples nothing until the sequences become its own (#2554).
 */
using TempoSequenceRippleBuilder = std::function<std::unique_ptr<UndoableCommand>(
    TempoSequenceRippleMode, BeatPosition start, BeatPosition end)>;

void setTempoSequenceRippleBuilder(TempoSequenceRippleBuilder builder);
void forgetTempoSequenceRippleBuilder();

/** @brief The ripple command, or null when nothing can build one. */
std::unique_ptr<UndoableCommand> makeTempoSequenceRippleCommand(TempoSequenceRippleMode mode,
                                                                BeatPosition start,
                                                                BeatPosition end);

}  // namespace magda
