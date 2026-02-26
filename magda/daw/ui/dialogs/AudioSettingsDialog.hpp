#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

namespace magda {

/**
 * Custom channel selector that shows both stereo pairs and individual mono channels
 * with mutual exclusion logic (can't select 1-2 AND 1 at the same time)
 */
class CustomChannelSelector : public juce::Component {
  public:
    CustomChannelSelector(juce::AudioDeviceManager* deviceManager, bool isInput,
                          tracktion::DeviceManager* teDeviceManager = nullptr);
    ~CustomChannelSelector() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void updateFromDevice();
    void applyToDevice();

  private:
    void onChannelToggled(int channelIndex, bool isStereo);
    void refreshChannelStates();

    juce::AudioDeviceManager* deviceManager_;
    tracktion::DeviceManager* teDeviceManager_;
    bool isInput_;

    void onPreviewToggled(int startChannel);

    struct ChannelToggle {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::ToggleButton> previewButton;  // Only for output stereo pairs
        int startChannel;                                   // 0-indexed
        bool isStereo;  // true = pair (e.g., 0-1), false = mono (e.g., 0)
    };

    std::vector<ChannelToggle> channelToggles_;
    juce::Label titleLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomChannelSelector)
};

/**
 * Dialog for configuring audio and MIDI device settings.
 * Uses custom channel selectors for fine-grained control.
 */
class AudioSettingsDialog : public juce::Component {
  public:
    explicit AudioSettingsDialog(juce::AudioDeviceManager* deviceManager,
                                 tracktion::DeviceManager* teDeviceManager = nullptr);
    ~AudioSettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent, juce::AudioDeviceManager* deviceManager,
                           tracktion::DeviceManager* teDeviceManager = nullptr);

  private:
    void populateDeviceLists();
    void onInputDeviceSelected();
    void onOutputDeviceSelected();
    void enableAllChannelsOnCurrentDevice();
    void savePreferencesIfNeeded();

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;
    std::unique_ptr<CustomChannelSelector> inputChannelSelector_;
    std::unique_ptr<CustomChannelSelector> outputChannelSelector_;

    juce::Label inputDeviceLabel_;
    juce::ComboBox inputDeviceComboBox_;
    juce::Label outputDeviceLabel_;
    juce::ComboBox outputDeviceComboBox_;
    juce::ToggleButton setAsPreferredCheckbox_;

    juce::TextButton closeButton_;
    juce::Label deviceNameLabel_;
    juce::AudioDeviceManager* deviceManager_;
    tracktion::DeviceManager* teDeviceManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsDialog)
};

}  // namespace magda
