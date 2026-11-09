// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "music/ChordEnums.hpp"
#include "ui/components/chord/ChordFunction.hpp"
#include "ui/components/chord/ChordWheel.hpp"
#include "ui/components/chord/FifthsStrip.hpp"

namespace magda::daw::ui {

/** @brief The chord the circle of fifths is drawn from, and how to voice what is picked. */
struct FifthsSource {
    magda::music::ChordRoot root = magda::music::ChordRoot::C;
    magda::music::ChordQuality quality = magda::music::ChordQuality::Major;
    juce::String name;
    std::optional<chord_function::Key> key;
    int octave = 4;
};

/**
 * @brief The circle of fifths in a panel: a wheel where there is room for one, else the two rows.
 *
 * Clicking plays a chord; dragging one out hands it to onDragOut once per press.
 */
class FifthsView : public juce::Component {
  public:
    using ChordHandler = std::function<void(magda::music::ChordRoot, magda::music::ChordQuality)>;

    FifthsView();

    /** @brief Draw from @p source, or show how to get one when there is none. */
    void setSource(std::optional<FifthsSource> source);
    const std::optional<FifthsSource>& getSource() const {
        return source_;
    }

    ChordHandler onAuditionStart;
    std::function<void()> onAuditionStop;
    ChordHandler onDragOut;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    ChordWheel wheel_;
    FifthsStrip strip_;
    std::optional<FifthsSource> source_;
    bool dragHandedOut_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FifthsView)
};

}  // namespace magda::daw::ui
