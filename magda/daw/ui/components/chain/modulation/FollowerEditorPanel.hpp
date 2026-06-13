#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ModInfo.hpp"
#include "ui/components/chain/modulation/ModulatorEditorPanel.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Editor panel for the envelope follower modulator.
 *
 * The follower is fundamentally different from the generator modulators (LFO /
 * Curve / Envelope / Random): it tracks the amplitude of its host scope's audio
 * continuously, so it has no waveform, rate, tempo sync, trigger mode, or MIDI.
 * It therefore gets its own panel rather than sharing ModulatorEditorPanel's
 * LFO-centric machinery. It reuses the generic value-history display
 * (RandomDisplay) and the links table (ModMatrixContent) from there.
 *
 * Layout:
 * +------------------+
 * |    MOD NAME      |
 * |  [live output]   |  <- scrolling envelope value
 * |  Gain  [-..+ dB] |
 * |  Atk      Hold   |
 * |  Release         |
 * |  Links...        |  <- mod matrix
 * +------------------+
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

    void setParamNameResolver(std::function<juce::String(magda::DeviceId, int)> resolver) {
        paramNameResolver_ = std::move(resolver);
    }

    // Callbacks
    std::function<void(juce::String name)> onNameChanged;
    std::function<void(const magda::ModInfo& mod)> onFollowerChanged;
    std::function<void(int modIndex, magda::ControlTarget target)> onModLinkDeleted;
    std::function<void(int modIndex, magda::ControlTarget target, bool bipolar)>
        onModLinkBipolarChanged;
    std::function<void(int modIndex, magda::ControlTarget target, bool enabled)>
        onModLinkEnabledChanged;
    std::function<void(int modIndex, magda::ControlTarget target, float amount)>
        onModLinkAmountChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int PREFERRED_WIDTH = 150;

  private:
    void updateFromMod();
    void updateModMatrix();
    void onNameLabelEdited();
    void fireFollowerChanged();

    int selectedModIndex_ = -1;
    magda::ModInfo currentMod_;
    const magda::ModInfo* liveModPtr_ = nullptr;
    std::function<const magda::ModInfo*()> liveModGetter_;

    juce::Label nameLabel_;
    RandomDisplay followerDisplay_;  // generic live value-history scroller
    TextSlider gainSlider_{TextSlider::Format::Decimal};
    TextSlider attackSlider_{TextSlider::Format::Decimal};
    TextSlider holdSlider_{TextSlider::Format::Decimal};
    TextSlider releaseSlider_{TextSlider::Format::Decimal};

    juce::Viewport modMatrixViewport_;
    ModMatrixContent modMatrixContent_;
    std::function<juce::String(magda::DeviceId, int)> paramNameResolver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FollowerEditorPanel)
};

}  // namespace magda::daw::ui
