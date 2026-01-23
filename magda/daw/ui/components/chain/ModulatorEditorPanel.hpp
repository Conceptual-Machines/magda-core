#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ModInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Panel for editing modulator settings
 *
 * Shows when a mod is selected from the mods panel.
 * Displays type selector, rate control, and target info.
 *
 * Layout:
 * +------------------+
 * |    MOD NAME      |  <- Header with mod name
 * +------------------+
 * | Type: [LFO   v]  |  <- Type selector
 * +------------------+
 * |   Rate: 1.0 Hz   |  <- Rate slider
 * +------------------+
 * | Target: Device   |  <- Target info
 * |   Param Name     |
 * +------------------+
 */
class ModulatorEditorPanel : public juce::Component {
  public:
    ModulatorEditorPanel();
    ~ModulatorEditorPanel() override = default;

    // Set the mod to edit
    void setModInfo(const magda::ModInfo& mod);

    // Set the selected mod index (-1 for none)
    void setSelectedModIndex(int index);
    int getSelectedModIndex() const {
        return selectedModIndex_;
    }

    // Callbacks
    std::function<void(magda::ModType type)> onTypeChanged;
    std::function<void(float rate)> onRateChanged;
    std::function<void(magda::LFOWaveform waveform)> onWaveformChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Preferred width for this panel
    static constexpr int PREFERRED_WIDTH = 120;

  private:
    int selectedModIndex_ = -1;
    magda::ModInfo currentMod_;

    // UI Components
    juce::Label nameLabel_;
    juce::ComboBox typeSelector_;
    juce::ComboBox waveformCombo_;
    TextSlider rateSlider_{TextSlider::Format::Decimal};
    juce::Label targetLabel_;

    void updateFromMod();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorEditorPanel)
};

}  // namespace magda::daw::ui
