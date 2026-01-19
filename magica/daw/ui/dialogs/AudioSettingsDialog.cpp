#include "AudioSettingsDialog.hpp"

#include "../themes/DarkTheme.hpp"

namespace magica {

AudioSettingsDialog::AudioSettingsDialog(juce::AudioDeviceManager* deviceManager) {
    // Create the device selector component
    // Parameters: deviceManager, minAudioInputChannels, maxAudioInputChannels,
    //             minAudioOutputChannels, maxAudioOutputChannels,
    //             showMidiInputOptions, showMidiOutputSelector,
    //             showChannelsAsStereoPairs, hideAdvancedOptionsWithButton
    deviceSelector_ =
        std::make_unique<juce::AudioDeviceSelectorComponent>(*deviceManager,
                                                             0,  // minAudioInputChannels
                                                             2,  // maxAudioInputChannels (stereo)
                                                             0,  // minAudioOutputChannels
                                                             2,  // maxAudioOutputChannels (stereo)
                                                             true,  // showMidiInputOptions
                                                             true,  // showMidiOutputSelector
                                                             true,  // showChannelsAsStereoPairs
                                                             false  // hideAdvancedOptionsWithButton
        );
    addAndMakeVisible(*deviceSelector_);

    // Setup close button
    closeButton_.setButtonText("Close");
    closeButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);

    // Set preferred size
    setSize(500, 550);
}

AudioSettingsDialog::~AudioSettingsDialog() = default;

void AudioSettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void AudioSettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Button at bottom
    const int buttonHeight = 28;
    const int buttonWidth = 80;
    auto buttonArea = bounds.removeFromBottom(buttonHeight);
    bounds.removeFromBottom(10);  // spacing

    // Center the close button
    closeButton_.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, buttonHeight));

    // Device selector takes the rest
    deviceSelector_->setBounds(bounds);
}

void AudioSettingsDialog::showDialog(juce::Component* parent,
                                     juce::AudioDeviceManager* deviceManager) {
    if (deviceManager == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Audio Settings",
            "Audio engine not initialized. Cannot open audio settings.");
        return;
    }

    auto* dialog = new AudioSettingsDialog(deviceManager);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio/MIDI Settings";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

}  // namespace magica
