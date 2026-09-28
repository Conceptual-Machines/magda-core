#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../audio/io/AudioIOControl.hpp"

namespace magda {

class AudioEngine;

/**
 * @brief The MIDI inputs available, ticked where Config has them active (#2755).
 *
 * MAGDA's own rather than the one juce::AudioDeviceSelectorComponent drew, so the choice
 * is Config's and JUCE is told about it rather than asked.
 */
class MidiInputList final : public juce::Component {
  public:
    explicit MidiInputList(AudioIOControl& audio);

    void resized() override;

    /** @brief Re-read the devices present and what Config says about them. */
    void refresh();

    /** @brief What the rows need, so the section packs under the list rather than around it. */
    int preferredHeight() const;

  private:
    void toggle(int index);

    AudioIOControl& audio_;
    juce::Viewport viewport_;
    juce::Component rows_;
    juce::Array<juce::MidiDeviceInfo> devices_;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;
};

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
class AudioSettingsDialog : public juce::Component, private HardwareChannels::Listener {
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
    void hardwareChannelsChanged() override;

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent, AudioEngine* audioEngine);

  private:
    void populateDeviceLists();
    void updateDevicePickerMode();
    void showDeviceRefreshIndicator(bool flushRepaint);
    void hideDeviceRefreshIndicator();
    void onInputDeviceSelected();
    void onOutputDeviceSelected();
    void onDriverSelected();
    void onSampleRateSelected();
    void onBufferSizeSelected();

    /** @brief List the rates and block sizes the open interface offers. */
    void populateStreamLists();
    void populateMidiOutputs();
    void refreshMidiControls();

    /** @brief Apply @p settings, saying so when the device refuses the stream it offered. */
    void applyStreamChange(const AudioIOSettings& settings, const juce::String& what,
                           const juce::String& asked);

    /** @brief Open @p interfaceName one way, keeping the channels chosen where it has them. */
    void chooseInterface(const juce::String& interfaceName, bool inputs);

    /** @brief Relist the interfaces and their channels, after the backend or an interface moved. */
    void refreshChosenInterface();
    void showOpenInterface();
    void savePreferencesIfNeeded();
    void onAudioEngineSelected();

    std::unique_ptr<MidiInputList> midiInputList_;
    std::unique_ptr<CustomChannelSelector> inputChannelSelector_;
    std::unique_ptr<CustomChannelSelector> outputChannelSelector_;

    juce::Label driverLabel_;
    juce::ComboBox driverComboBox_;
    juce::Label inputDeviceLabel_;
    juce::ComboBox inputDeviceComboBox_;
    juce::Label outputDeviceLabel_;
    juce::ComboBox outputDeviceComboBox_;
    double deviceRefreshProgress_ = -1.0;
    juce::ProgressBar deviceRefreshSpinner_;
    juce::Label deviceRefreshLabel_;
    juce::ToggleButton setAsPreferredCheckbox_;

    juce::Label sampleRateLabel_;
    juce::ComboBox sampleRateComboBox_;
    juce::Label bufferSizeLabel_;
    juce::ComboBox bufferSizeComboBox_;

    juce::Label midiInputsLabel_;
    juce::Label midiOutputLabel_;
    juce::ComboBox midiOutputComboBox_;
    juce::TextButton bluetoothMidiButton_;

    /// Listed with the output items, so a selection never indexes a list that has moved.
    std::vector<juce::String> midiOutputIds_;

    /// MIDI hot-plug and Bluetooth pairing, which no audio notification covers.
    juce::MidiDeviceListConnection midiDevices_ =
        juce::MidiDeviceListConnection::make([this] { refreshMidiControls(); });

    // Which engine renders. Here because it is an audio-device-level choice and
    // this is where a user already comes to change one (#2559).
    juce::Label engineLabel_;
    juce::ComboBox engineComboBox_;
    juce::Label engineRestartLabel_;

    juce::TextButton closeButton_;
    juce::Label deviceNameLabel_;
    AudioEngine* audioEngine_;
    AudioIOControl* audio_;

    /// The backend and interfaces the lists show, so a channel toggle does not rebuild them.
    AudioIOSettings listed_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsDialog)
};

}  // namespace magda
