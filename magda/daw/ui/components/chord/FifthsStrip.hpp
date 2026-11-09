// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "music/ChordEnums.hpp"
#include "ui/components/chord/ChordFunction.hpp"
#include "ui/components/chord/CircleOfFifths.hpp"

namespace magda::daw::ui {

/**
 * @brief The circle of fifths unrolled for a short panel: majors in a row over their relative
 * minors, scrolled so the chosen chord sits in the middle.
 *
 * Plays and drags like ChordWheel: click to hear, drag out to add, ←/→ or the scroll wheel move.
 */
class FifthsStrip : public juce::Component, private juce::Timer {
  public:
    using ChordHandler = std::function<void(magda::music::ChordRoot, magda::music::ChordQuality)>;

    FifthsStrip();

    /** @brief Centre the strip on the chord a pick follows. */
    void setSource(magda::music::ChordRoot root, magda::music::ChordQuality quality,
                   std::optional<chord_function::Key> key);

    ChordHandler onAuditionStart;
    std::function<void()> onAuditionStop;
    /// Fires on every move once a press has become a drag.
    ChordHandler onDragMove;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

  private:
    using Segment = circle_of_fifths::Segment;

    void timerCallback() override;
    void scrollBy(int steps);
    float cellWidth() const;
    Segment segmentAt(juce::Point<float> position) const;
    juce::Rectangle<float> cellBounds(circle_of_fifths::Ring ring, int unwrappedStep) const;

    std::optional<chord_function::Key> key_;
    circle_of_fifths::Ring sourceRing_ = circle_of_fifths::Ring::Outer;
    /// Unwrapped, like ChordWheel's, so scrolling past F carries on to C.
    int targetStep_ = 0;
    float scroll_ = 0.0f;
    float scrollAccumulator_ = 0.0f;
    Segment hovered_;
    Segment pressed_;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FifthsStrip)
};

}  // namespace magda::daw::ui
