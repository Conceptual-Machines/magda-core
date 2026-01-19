#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace magica {

/**
 * Dialog for configuring audio and MIDI device settings.
 * Uses JUCE's AudioDeviceSelectorComponent for device selection.
 */
class AudioSettingsDialog : public juce::Component {
  public:
    explicit AudioSettingsDialog(juce::AudioDeviceManager* deviceManager);
    ~AudioSettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent, juce::AudioDeviceManager* deviceManager);

  private:
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;
    juce::TextButton closeButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsDialog)
};

}  // namespace magica
