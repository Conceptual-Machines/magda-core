// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TypeIds.hpp"

namespace magda {

/**
 * @brief Folds or unfolds the chord track in Session View and the Mixer (#3228).
 *
 * Deferred, because the views rebuild their strips in response and the caller is
 * usually one of those strips' buttons.
 */
void setChordTrackFolded(TrackId trackId, bool folded);

/** @brief A bare chevron: folds the chord track, or expands and collapses a group's children. */
class FoldChevronButton : public juce::Button {
  public:
    enum class Direction { Left, Right, Down };

    explicit FoldChevronButton(Direction direction);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

  private:
    Direction direction_;
};

/** @brief The chord track folded to a narrow strip: a chevron and its name. A click unfolds it. */
class FoldedChordStrip : public juce::Component, public juce::SettableTooltipClient {
  public:
    static constexpr int WIDTH = 24;

    explicit FoldedChordStrip(TrackId trackId);

    TrackId getTrackId() const {
        return trackId_;
    }

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;

  private:
    TrackId trackId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FoldedChordStrip)
};

}  // namespace magda
