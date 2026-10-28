// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief An ADSR drawn as its shape, its stages dragged at their handles (#3100).
 *
 * The amp, pitch and filter envelopes' plot on the Sampler faceplate. A drag edits the stages'
 * sliders, which carry the parameters and their mod links, so the shape is whatever they hold.
 */
class SamplerEnvelopeView : public juce::Component {
  public:
    SamplerEnvelopeView() = default;

    struct Stages {
        LinkableTextSlider* attack = nullptr;
        LinkableTextSlider* decay = nullptr;
        LinkableTextSlider* sustain = nullptr;
        LinkableTextSlider* release = nullptr;
        /// A depth in semitones, shown above the shape; negative hangs it from the top. Null for
        /// an envelope with none, as the amp's.
        const LinkableTextSlider* amount = nullptr;
    };
    void setStages(const Stages& stages);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

  private:
    enum class Handle { None, Attack, Decay, Release };
    struct Shape {
        juce::Point<float> start, peak, decayEnd, releaseStart, end;
    };
    juce::Rectangle<float> plotArea() const;
    bool inverted() const;
    float levelAt(float y) const;
    std::optional<Shape> shape() const;
    Handle handleAt(juce::Point<float> position) const;
    void drag(Handle handle, juce::Point<float> position);

    Stages stages_;
    Handle dragging_ = Handle::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerEnvelopeView)
};

}  // namespace magda::daw::ui
