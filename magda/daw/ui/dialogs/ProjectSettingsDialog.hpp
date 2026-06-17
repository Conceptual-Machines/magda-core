#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Per-project settings dialog (File > Project Settings).
 *
 * Edits the authoritative per-project values held in ProjectInfo: total timeline
 * length (bars), working/render sample rate, and render / bounce bit depth.
 * New projects seed these from the global Config defaults; this dialog overrides
 * them for the current project.
 */
class ProjectSettingsDialog : public juce::Component {
  public:
    ProjectSettingsDialog();
    ~ProjectSettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(juce::Component* parent);

  private:
    void loadSettings();
    void applySettings();

    juce::Label lengthLabel_, sampleRateLabel_, renderBitLabel_, bounceBitLabel_;
    juce::Slider lengthSlider_;
    juce::ComboBox sampleRateCombo_, renderBitCombo_, bounceBitCombo_;
    juce::ToggleButton saveAsDefaultBtn_;
    juce::TextButton okBtn_{"OK"}, cancelBtn_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSettingsDialog)
};

}  // namespace magda
