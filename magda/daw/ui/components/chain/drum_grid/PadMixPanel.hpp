// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/DrumGridPads.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/** @brief The selected pad's mix as the Drum Grid shows it beside the pad's chain. */
struct PadMixView {
    juce::String name;
    juce::String note;
    /// The note the pad sends its devices (#3432).
    int rootNote = magda::kPadRootNote;
    int colourIndex = 0;
    float volume = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    bool bypassed = false;
};

/**
 * @brief A column for the selected pad: its name and note over gain, pan, M, S and power,
 *        which are its channel's mix (#3144).
 */
class PadMixPanel : public juce::Component {
  public:
    PadMixPanel();

    void setPad(const PadMixView& pad);
    /// An empty pad shows its name and note only: there is nothing to mix yet.
    void setMixable(bool mixable);

    /// (gainDb, pan) on every move; a drag's end arrives as onGestureEnd.
    std::function<void(float, float)> onMixChanged;
    std::function<void()> onGestureEnd;
    std::function<void(bool mute, bool solo, bool bypassed)> onSwitchesChanged;
    /// The pad's root, once a drag or an entry ends: each one rebuilds the track's chain.
    std::function<void(int rootNote)> onRootChanged;
    /// The pad named here; an empty name unnames it.
    std::function<void(const juce::String&)> onRenamed;

    /// Edit the pad's name in place, when there is a pad to name.
    void startRename();

    static constexpr int kWidth = 200;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

  private:
    void styleControls();
    void reportSwitches();
    void reportRoot();
    juce::Rectangle<int> nameTextArea() const;
    void finishRename(bool commit);

    PadMixView pad_;
    bool mixable_ = false;
    bool dragging_ = false;
    bool rootDragging_ = false;
    juce::Rectangle<int> nameArea_, noteArea_, rootCaption_, gainCaption_, panCaption_;
    std::unique_ptr<juce::TextEditor> nameEditor_;

    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    magda::DraggableValueLabel panLabel_{magda::DraggableValueLabel::Format::Pan};
    magda::DraggableValueLabel rootLabel_{magda::DraggableValueLabel::Format::MidiNote};
    magda::SvgButton muteButton_{"mute", BinaryData::master_on_svg, BinaryData::master_on_svgSize};
    magda::SvgButton soloButton_{"solo", BinaryData::solo_svg, BinaryData::solo_svgSize};
    magda::SvgButton powerButton_{"Power", BinaryData::power_svg, BinaryData::power_svgSize};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadMixPanel)
};

}  // namespace magda::daw::ui
