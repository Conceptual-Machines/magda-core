// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "../audio/io/XrunLog.hpp"

namespace magda::remote {

/**
 * @brief engine.health's xrun problems: the log's events, and what the polled count adds.
 *
 * The log can miss a rise, one counted while no callback runs or one dropped from a full
 * queue. What the count rose by and the log has not explained by the next read is reported
 * then, stamped with the read that first saw it and without callbacks (#3386).
 */
class XrunProblems {
  public:
    /// The log's events since the last read, then how far the count rose since it; returns
    /// the problems to add, oldest first.
    std::vector<juce::var> read(const std::vector<XrunLog::Event>& events, int risen,
                                juce::int64 nowMs);

    void reset();

  private:
    juce::int64 logged_ = 0;
    juce::int64 owed_ = 0;
    juce::int64 owedAtMs_ = 0;
};

/// "overran" when a callback before the xrun ran past its length, "writerWait" when waiting
/// on the render-ahead thread is what took it past, "outsideRender" when every one finished
/// in time. A hint: the device does not say which callback it blames.
const char* likelyCause(const XrunLog::Event& event);

juce::var xrunProblem(const XrunLog::Event& event);

/// A problem with no callbacks, for a rise the log did not explain.
juce::var plainProblem(const char* code, juce::int64 atMs, int count);

}  // namespace magda::remote
