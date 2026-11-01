// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ModInfo.hpp"
#include "ui/components/chain/modulation/ModulatorEditorPanel.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief The envelope follower's controls, the controls tab of ModulatorEditorPanel.
 *
 * The follower tracks its source's amplitude continuously, so it has no waveform, rate, tempo
 * sync, trigger mode or MIDI. The panel that hosts it shows its name and its links.
 */
class FollowerEditorPanel : public juce::Component {
  public:
    FollowerEditorPanel();
    ~FollowerEditorPanel() override = default;

    // liveModGetter provides a safe way to fetch the live mod pointer on demand
    // (avoids dangling pointers when the mod vector reallocates).
    void setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod = nullptr,
                    std::function<const magda::ModInfo*()> liveModGetter = nullptr);

    void setSelectedModIndex(int index) {
        selectedModIndex_ = index;
    }
    int getSelectedModIndex() const {
        return selectedModIndex_;
    }

    // Callbacks
    std::function<void(const magda::ModInfo& mod)> onFollowerChanged;
    // Fires when the user clicks the audio-source button; the host opens the
    // sidechain-source picker (Self / a track) which sets the host device's
    // sidechain. "External" source drives the follower from that track's level.
    std::function<void()> onSourceClicked;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void updateFromMod();
    void fireFollowerChanged();

    int selectedModIndex_ = -1;
    magda::ModInfo currentMod_;
    const magda::ModInfo* liveModPtr_ = nullptr;
    std::function<const magda::ModInfo*()> liveModGetter_;

    RandomDisplay followerDisplay_;  // generic live value-history scroller
    juce::TextButton sourceButton_{"Audio Source"};
    TextSlider gainSlider_{TextSlider::Format::Decimal};
    TextSlider attackSlider_{TextSlider::Format::Decimal};
    TextSlider holdSlider_{TextSlider::Format::Decimal};
    TextSlider releaseSlider_{TextSlider::Format::Decimal};

    // Band-limit detection: filter the source audio before peak detection so the
    // follower tracks just part of the spectrum. Toggle + cutoff per band.
    juce::TextButton hpEnableButton_{"HP"};
    TextSlider hpFreqSlider_{TextSlider::Format::Decimal};
    juce::TextButton lpEnableButton_{"LP"};
    TextSlider lpFreqSlider_{TextSlider::Format::Decimal};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FollowerEditorPanel)
};

}  // namespace magda::daw::ui
