#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * @brief Pre-pass dialog for a gain-staging run.
 *
 * Asks for the target headroom and whether to use the AI agent, then hands the
 * choices back via onStart so the caller can configure and start the pass.
 */
class GainStagingDialog : public juce::Component {
  public:
    struct Settings {
        float targetDb = -12.0f;
        bool useAi = false;
    };

    GainStagingDialog(float initialTargetDb, bool initialUseAi);
    ~GainStagingDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    Settings getSettings() const;

    std::function<void(const Settings&)> onStart;

    static void showDialog(juce::Component* parent, float initialTargetDb, bool initialUseAi,
                           std::function<void(const Settings&)> startCallback);

  private:
    juce::Label descriptionLabel_;
    juce::Label targetLabel_;
    juce::Slider targetSlider_;
    juce::ToggleButton useAiButton_;
    juce::Label aiHintLabel_;
    juce::TextButton startButton_;
    juce::TextButton cancelButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainStagingDialog)
};

}  // namespace magda
