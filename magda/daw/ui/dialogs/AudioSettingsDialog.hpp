#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../audio/io/AudioIOControl.hpp"
#include "../../audio/midi/ActiveMidiInputs.hpp"

namespace magda {

class AudioEngine;

/**
 * @brief The chosen interface's channels one way, as stereo pairs and mono channels.
 *
 * A pair and either of its channels exclude each other. What is ticked is exactly what opens
 * (#2749).
 */
class CustomChannelSelector : public juce::Component {
  public:
    CustomChannelSelector(AudioIOControl& audio, bool isInput);
    ~CustomChannelSelector() override;

    void resized() final;
    void paint(juce::Graphics& g) override;

    /** @brief List the chosen interface's channels, ticked where they are chosen. */
    void refresh();

  private:
    void onChannelToggled(int channelIndex, bool isStereo);
    void refreshChannelStates();

    /** @brief Open exactly the ticked channels on the chosen interface. */
    void applyTicks();

    AudioIOControl& audio_;
    bool isInput_;

    static void onPreviewToggled(int startChannel);

    struct ChannelToggle {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::ToggleButton> previewButton;  // Only for output stereo pairs
        int startChannel;                                   // 0-indexed
        bool isStereo;  // true = pair (e.g., 0-1), false = mono (e.g., 0)
    };

    /// Positions the rows inside @ref list_ across @p width.
    void layOutRows(int width);

    /// How tall the rows are. Independent of how wide they are, which is what
    /// lets the scrollbar be settled before the width is measured.
    int rowsHeight() const;

    std::vector<ChannelToggle> channelToggles_;
    juce::Label titleLabel_;

    /// A device has more rows than the dialog has room for: eight channels is
    /// four pairs plus eight monos against space for about seven. The rows are
    /// children of a component the viewport scrolls, rather than of this one,
    /// because a viewport moves a single component and not a set of siblings.
    juce::Viewport viewport_;
    juce::Component list_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomChannelSelector)
};

/**
 * Dialog for configuring audio and MIDI device settings.
 * Uses custom channel selectors for fine-grained control.
 */
class AudioSettingsDialog : public juce::Component,
                            private juce::ChangeListener,
                            private juce::ComboBox::Listener {
  public:
    explicit AudioSettingsDialog(AudioEngine* audioEngine);
    ~AudioSettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // The hosting window's background is a colour override handed over once at
    // construction, so it does not follow a live theme switch on its own.
    void lookAndFeelChanged() override;

    // Re-list the device combos when the driver type or device changes (e.g. the
    // user picks a different driver in the AudioDeviceSelectorComponent).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent, AudioEngine* audioEngine);

  private:
    void populateDeviceLists();
    void updateDevicePickerMode();
    void attachDriverTypeComboListener();
    void detachDriverTypeComboListener();
    void showDeviceRefreshIndicator(bool flushRepaint);
    void hideDeviceRefreshIndicator();
    void onInputDeviceSelected();
    void onOutputDeviceSelected();

    /** @brief Open @p interfaceName one way, keeping the channels chosen where it has them. */
    void chooseInterface(const juce::String& interfaceName, bool inputs);

    /** @brief Keep the backend, rate and block size the JUCE selector changed on the manager. */
    void keepSelectorChanges();

    void refreshChosenInterface();
    void savePreferencesIfNeeded();
    void onAudioEngineSelected();

    /// Before the selector, which reads the ticks it sets.
    std::unique_ptr<ActiveMidiInputs> activeMidiInputs_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;
    std::unique_ptr<CustomChannelSelector> inputChannelSelector_;
    std::unique_ptr<CustomChannelSelector> outputChannelSelector_;

    juce::Label inputDeviceLabel_;
    juce::ComboBox inputDeviceComboBox_;
    juce::Label outputDeviceLabel_;
    juce::ComboBox outputDeviceComboBox_;
    double deviceRefreshProgress_ = -1.0;
    juce::ProgressBar deviceRefreshSpinner_;
    juce::Label deviceRefreshLabel_;
    juce::ToggleButton setAsPreferredCheckbox_;

    // Which engine renders. Here because it is an audio-device-level choice and
    // this is where a user already comes to change one (#2559).
    juce::Label engineLabel_;
    juce::ComboBox engineComboBox_;
    juce::Label engineRestartLabel_;

    juce::TextButton closeButton_;
    juce::Label deviceNameLabel_;
    juce::AudioDeviceManager* deviceManager_;
    AudioEngine* audioEngine_;
    AudioIOControl* audio_;
    juce::ComboBox* driverTypeComboBox_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsDialog)
};

}  // namespace magda
