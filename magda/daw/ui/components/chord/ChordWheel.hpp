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
 * @brief The circle of fifths around a chord: majors on the outer ring, relative minors inside.
 *
 * The chord it starts from sits at the top. ←/→ or the scroll wheel turn another segment to the
 * top. Clicking a segment plays it; dragging one out reports where it is let go, and Return picks
 * the top one.
 */
class ChordWheel : public juce::Component, private juce::Timer {
  public:
    using ChordHandler = std::function<void(magda::music::ChordRoot, magda::music::ChordQuality)>;
    /// The chord being dragged and the pointer, in this component's coordinates.
    using DragHandler =
        std::function<void(magda::music::ChordRoot, magda::music::ChordQuality, juce::Point<int>)>;

    ChordWheel();

    /** @brief Centre the wheel on the chord a pick follows, with that chord at the top. */
    void setSource(magda::music::ChordRoot root, magda::music::ChordQuality quality,
                   const juce::String& name, std::optional<chord_function::Key> key);

    /** @brief A segment's label: the root, with "m" for a minor, in the current note naming. */
    static juce::String labelFor(magda::music::ChordRoot root, magda::music::ChordQuality quality);

    ChordHandler onPick;
    /// Pressing a segment sounds its chord until onAuditionStop or the drag starts.
    ChordHandler onAuditionStart;
    std::function<void()> onAuditionStop;
    DragHandler onDragMove;
    DragHandler onDragEnd;
    std::function<void()> onDismiss;

    /** @brief The disc's radius: the component is square, with room for a shadow around it. */
    float discRadius() const {
        return radii().outer;
    }

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
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
    void rotateBy(int steps);
    circle_of_fifths::Radii radii() const;
    Segment segmentAt(juce::Point<float> position) const;
    Segment topSegment() const;
    void paintRing(juce::Graphics& g, circle_of_fifths::Ring ring) const;
    void paintHub(juce::Graphics& g) const;

    juce::String sourceName_;
    juce::String sourceDegree_;
    std::optional<chord_function::Key> key_;
    circle_of_fifths::Ring sourceRing_ = circle_of_fifths::Ring::Outer;
    /// Unwrapped, so a turn past F animates on to C rather than back round.
    int targetStep_ = 0;
    float rotation_ = 0.0f;
    float scrollAccumulator_ = 0.0f;
    Segment hovered_;
    Segment pressed_;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordWheel)
};

}  // namespace magda::daw::ui
