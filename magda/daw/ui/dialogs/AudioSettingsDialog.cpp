#include "AudioSettingsDialog.hpp"

#include "../../core/Config.hpp"
#include "../themes/DarkTheme.hpp"

namespace magda {

// ============================================================================
// CustomChannelSelector Implementation
// ============================================================================

CustomChannelSelector::CustomChannelSelector(juce::AudioDeviceManager* deviceManager, bool isInput,
                                             tracktion::DeviceManager* teDeviceManager)
    : deviceManager_(deviceManager), teDeviceManager_(teDeviceManager), isInput_(isInput) {
    titleLabel_.setText(isInput ? "Audio Inputs:" : "Audio Outputs:", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel_);

    updateFromDevice();
}

CustomChannelSelector::~CustomChannelSelector() = default;

void CustomChannelSelector::updateFromDevice() {
    // Clear existing toggles
    channelToggles_.clear();

    auto* device = deviceManager_->getCurrentAudioDevice();
    if (!device) {
        DBG("CustomChannelSelector::updateFromDevice - No current audio device!");
        return;
    }

    DBG("CustomChannelSelector::updateFromDevice - Device: " + device->getName() +
        " (isInput=" + juce::String(isInput_ ? "true" : "false") + ")");

    // Get channel names from hardware device
    auto channelNames = isInput_ ? device->getInputChannelNames() : device->getOutputChannelNames();

    // Build active channels from TE device enabled state (JUCE bits are always all-on)
    juce::BigInteger activeChannels;
    if (teDeviceManager_) {
        auto setEnabledBits = [&activeChannels](auto devices) {
            for (auto* dev : devices) {
                if (dev->isEnabled()) {
                    for (const auto& ch : dev->getChannels())
                        activeChannels.setBit(ch.indexInDevice, true);
                }
            }
        };
        if (isInput_)
            setEnabledBits(teDeviceManager_->getWaveInputDevices());
        else
            setEnabledBits(teDeviceManager_->getWaveOutputDevices());
    } else {
        // Fallback: read from JUCE setup
        auto setup = deviceManager_->getAudioDeviceSetup();
        activeChannels = isInput_ ? setup.inputChannels : setup.outputChannels;
    }

    DBG("  Channel count: " + juce::String(channelNames.size()));
    DBG("  Active channels: " + activeChannels.toString(2));
    for (int i = 0; i < channelNames.size(); ++i) {
        DBG("    Channel " + juce::String(i) + ": " + channelNames[i]);
    }

    // Show ALL available channels from the device, regardless of which are currently active
    // The user can then select which ones to enable/disable
    int numChannels = channelNames.size();

    // Create stereo pair toggles first
    for (int i = 0; i < numChannels; i += 2) {
        if (i + 1 < numChannels) {
            ChannelToggle toggle;
            toggle.button = std::make_unique<juce::ToggleButton>(juce::String(i + 1) + "-" +
                                                                 juce::String(i + 2));
            toggle.startChannel = i;
            toggle.isStereo = true;

            // Check if both channels in pair are active
            bool pairActive = activeChannels[i] && activeChannels[i + 1];
            toggle.button->setToggleState(pairActive, juce::dontSendNotification);

            toggle.button->onClick = [this, i]() { onChannelToggled(i, true); };
            addAndMakeVisible(*toggle.button);
            channelToggles_.push_back(std::move(toggle));
        }
    }

    // Create individual mono channel toggles
    for (int i = 0; i < numChannels; ++i) {
        ChannelToggle toggle;
        toggle.button = std::make_unique<juce::ToggleButton>(juce::String(i + 1) + " (mono)");
        toggle.startChannel = i;
        toggle.isStereo = false;

        // Check if this individual channel is active (and its pair is not)
        bool monoActive = activeChannels[i];
        if (i % 2 == 0 && i + 1 < numChannels) {
            // Even channel - check if pair is active
            monoActive = monoActive && !activeChannels[i + 1];
        } else if (i % 2 == 1) {
            // Odd channel - check if pair is active
            monoActive = monoActive && !activeChannels[i - 1];
        }

        toggle.button->setToggleState(monoActive, juce::dontSendNotification);
        toggle.button->onClick = [this, i]() { onChannelToggled(i, false); };
        addAndMakeVisible(*toggle.button);
        channelToggles_.push_back(std::move(toggle));
    }

    refreshChannelStates();
    resized();
}

void CustomChannelSelector::onChannelToggled(int channelIndex, bool isStereo) {
    if (isStereo) {
        // Stereo pair toggled - find corresponding mono channels and disable/uncheck them
        for (auto& toggle : channelToggles_) {
            if (!toggle.isStereo) {
                if (toggle.startChannel == channelIndex ||
                    toggle.startChannel == channelIndex + 1) {
                    // This mono channel conflicts with the stereo pair
                    bool stereoEnabled = false;
                    // Find the stereo toggle to check its state
                    for (const auto& stereoToggle : channelToggles_) {
                        if (stereoToggle.isStereo && stereoToggle.startChannel == channelIndex) {
                            stereoEnabled = stereoToggle.button->getToggleState();
                            break;
                        }
                    }

                    if (stereoEnabled) {
                        toggle.button->setToggleState(false, juce::dontSendNotification);
                    }
                }
            }
        }
    } else {
        // Mono channel toggled - check if it conflicts with stereo pair
        int pairStartChannel = (channelIndex % 2 == 0) ? channelIndex : channelIndex - 1;

        // If this mono channel is enabled, disable the corresponding stereo pair
        for (auto& toggle : channelToggles_) {
            if (toggle.isStereo && toggle.startChannel == pairStartChannel) {
                bool monoEnabled = false;
                // Check if this mono channel is enabled
                for (const auto& monoToggle : channelToggles_) {
                    if (!monoToggle.isStereo && monoToggle.startChannel == channelIndex) {
                        monoEnabled = monoToggle.button->getToggleState();
                        break;
                    }
                }

                if (monoEnabled) {
                    toggle.button->setToggleState(false, juce::dontSendNotification);
                }
                break;
            }
        }
    }

    refreshChannelStates();
    applyToDevice();
}

void CustomChannelSelector::refreshChannelStates() {
    // Enable/disable toggles based on mutual exclusion rules
    for (auto& toggle : channelToggles_) {
        if (toggle.isStereo) {
            // Stereo pair - check if either mono channel is active
            bool monoConflict = false;
            for (const auto& monoToggle : channelToggles_) {
                if (!monoToggle.isStereo) {
                    if ((monoToggle.startChannel == toggle.startChannel ||
                         monoToggle.startChannel == toggle.startChannel + 1) &&
                        monoToggle.button->getToggleState()) {
                        monoConflict = true;
                        break;
                    }
                }
            }
            toggle.button->setEnabled(!monoConflict);
        } else {
            // Mono channel - check if stereo pair is active
            int pairStartChannel =
                (toggle.startChannel % 2 == 0) ? toggle.startChannel : toggle.startChannel - 1;
            bool stereoConflict = false;

            for (const auto& stereoToggle : channelToggles_) {
                if (stereoToggle.isStereo && stereoToggle.startChannel == pairStartChannel &&
                    stereoToggle.button->getToggleState()) {
                    stereoConflict = true;
                    break;
                }
            }
            toggle.button->setEnabled(!stereoConflict);
        }
    }
}

void CustomChannelSelector::applyToDevice() {
    auto* device = deviceManager_->getCurrentAudioDevice();
    if (!device)
        return;

    // Build BigInteger representing active channels
    juce::BigInteger activeChannels;

    for (const auto& toggle : channelToggles_) {
        if (toggle.button->getToggleState()) {
            if (toggle.isStereo) {
                // Stereo pair - enable both channels
                activeChannels.setBit(toggle.startChannel, true);
                activeChannels.setBit(toggle.startChannel + 1, true);
            } else {
                // Mono channel
                activeChannels.setBit(toggle.startChannel, true);
            }
        }
    }

    // Always enable all hardware channels at JUCE level — Tracktion expects this.
    // Channel selection is handled at the TE WaveInputDevice enable/disable level.
    auto setup = deviceManager_->getAudioDeviceSetup();
    if (isInput_) {
        setup.inputChannels.clear();
        setup.inputChannels.setRange(0, device->getInputChannelNames().size(), true);
    } else {
        setup.outputChannels.clear();
        setup.outputChannels.setRange(0, device->getOutputChannelNames().size(), true);
    }

    deviceManager_->setAudioDeviceSetup(setup, true);

    // Flush pending async updates so wave device list rebuilds before audio callback fires (#719)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(0);

    // Sync TE wave devices based on toggle state
    if (teDeviceManager_) {
        auto syncDevices = [&activeChannels](auto devices) {
            for (auto* dev : devices) {
                bool shouldEnable = false;
                for (const auto& ch : dev->getChannels()) {
                    if (activeChannels[ch.indexInDevice]) {
                        shouldEnable = true;
                        break;
                    }
                }
                if (dev->isEnabled() != shouldEnable)
                    dev->setEnabled(shouldEnable);
            }
        };
        if (isInput_)
            syncDevices(teDeviceManager_->getWaveInputDevices());
        else
            syncDevices(teDeviceManager_->getWaveOutputDevices());
    }
}

void CustomChannelSelector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
}

void CustomChannelSelector::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);

    const int toggleHeight = 24;
    const int spacing = 4;

    for (auto& toggle : channelToggles_) {
        toggle.button->setBounds(bounds.removeFromTop(toggleHeight));
        bounds.removeFromTop(spacing);
    }
}

// ============================================================================
// AudioSettingsDialog Implementation
// ============================================================================

AudioSettingsDialog::AudioSettingsDialog(juce::AudioDeviceManager* deviceManager,
                                         tracktion::DeviceManager* teDeviceManager)
    : deviceManager_(deviceManager), teDeviceManager_(teDeviceManager) {
    // Input device selection dropdown
    inputDeviceLabel_.setText("Input Device:", juce::dontSendNotification);
    inputDeviceLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(inputDeviceLabel_);

    inputDeviceComboBox_.onChange = [this]() { onInputDeviceSelected(); };
    addAndMakeVisible(inputDeviceComboBox_);

    // Output device selection dropdown
    outputDeviceLabel_.setText("Output Device:", juce::dontSendNotification);
    outputDeviceLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(outputDeviceLabel_);

    outputDeviceComboBox_.onChange = [this]() { onOutputDeviceSelected(); };
    addAndMakeVisible(outputDeviceComboBox_);

    populateDeviceLists();

    // "Set as preferred devices" checkbox
    setAsPreferredCheckbox_.setButtonText("Set as preferred devices (auto-select on startup)");
    addAndMakeVisible(setAsPreferredCheckbox_);

    // Check if current devices match preferred devices in Config
    auto& config = magda::Config::getInstance();
    auto setup = deviceManager->getAudioDeviceSetup();
    bool inputMatches = setup.inputDeviceName.toStdString() == config.getPreferredInputDevice();
    bool outputMatches = setup.outputDeviceName.toStdString() == config.getPreferredOutputDevice();
    setAsPreferredCheckbox_.setToggleState(inputMatches && outputMatches,
                                           juce::dontSendNotification);

    // Create the device selector component (MIDI only, no audio device selection)
    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        *deviceManager,
        0,      // minAudioInputChannels (0 = don't show channel selection)
        0,      // maxAudioInputChannels (0 = don't show channel selection)
        0,      // minAudioOutputChannels
        0,      // maxAudioOutputChannels (0 = don't show channel selection)
        true,   // showMidiInputOptions
        true,   // showMidiOutputSelector
        false,  // showChannelsAsStereoPairs
        false   // hideAdvancedOptionsWithButton
    );
    addAndMakeVisible(*deviceSelector_);

    // Create custom channel selectors for inputs and outputs
    inputChannelSelector_ =
        std::make_unique<CustomChannelSelector>(deviceManager, true, teDeviceManager);
    addAndMakeVisible(*inputChannelSelector_);

    outputChannelSelector_ =
        std::make_unique<CustomChannelSelector>(deviceManager, false, teDeviceManager);
    addAndMakeVisible(*outputChannelSelector_);

    // Setup device name label
    deviceNameLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    deviceNameLabel_.setJustificationType(juce::Justification::centred);
    if (auto* device = deviceManager->getCurrentAudioDevice()) {
        juce::String labelText = "Current Device: " + device->getName();
        labelText += " (" + juce::String(device->getInputChannelNames().size()) + " in, ";
        labelText += juce::String(device->getOutputChannelNames().size()) + " out)";
        deviceNameLabel_.setText(labelText, juce::dontSendNotification);
    } else {
        deviceNameLabel_.setText("No audio device selected", juce::dontSendNotification);
    }
    addAndMakeVisible(deviceNameLabel_);

    // Setup close button
    closeButton_.setButtonText("Close");
    closeButton_.onClick = [this]() {
        savePreferencesIfNeeded();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);

    // Set preferred size
    setSize(700, 700);
}

AudioSettingsDialog::~AudioSettingsDialog() = default;

void AudioSettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void AudioSettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Device name label at top
    deviceNameLabel_.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);  // spacing

    // Input device selection dropdown
    auto inputDeviceArea = bounds.removeFromTop(28);
    inputDeviceLabel_.setBounds(inputDeviceArea.removeFromLeft(120));
    inputDeviceArea.removeFromLeft(10);  // spacing
    inputDeviceComboBox_.setBounds(inputDeviceArea);
    bounds.removeFromTop(5);  // spacing

    // Output device selection dropdown
    auto outputDeviceArea = bounds.removeFromTop(28);
    outputDeviceLabel_.setBounds(outputDeviceArea.removeFromLeft(120));
    outputDeviceArea.removeFromLeft(10);  // spacing
    outputDeviceComboBox_.setBounds(outputDeviceArea);
    bounds.removeFromTop(5);  // spacing

    // "Set as preferred" checkbox
    setAsPreferredCheckbox_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(15);  // spacing

    // Close button at bottom
    const int buttonHeight = 28;
    const int buttonWidth = 80;
    auto buttonArea = bounds.removeFromBottom(buttonHeight);
    bounds.removeFromBottom(10);  // spacing
    closeButton_.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, buttonHeight));

    // Split remaining space: device selector on left, channel selectors on right
    auto deviceArea = bounds.removeFromLeft(bounds.getWidth() / 2);
    bounds.removeFromLeft(10);  // spacing

    // Device selector (MIDI selection)
    deviceSelector_->setBounds(deviceArea);

    // Channel selectors on the right, split vertically
    auto inputArea = bounds.removeFromTop(bounds.getHeight() / 2);
    bounds.removeFromTop(10);  // spacing

    inputChannelSelector_->setBounds(inputArea);
    outputChannelSelector_->setBounds(bounds);
}

void AudioSettingsDialog::populateDeviceLists() {
    inputDeviceComboBox_.clear();
    outputDeviceComboBox_.clear();

    auto& deviceTypes = deviceManager_->getAvailableDeviceTypes();
    if (deviceTypes.isEmpty())
        return;

    // Get first device type (CoreAudio on macOS)
    auto* deviceType = deviceTypes[0];
    deviceType->scanForDevices();

    auto inputDevices = deviceType->getDeviceNames(true);    // Get input devices
    auto outputDevices = deviceType->getDeviceNames(false);  // Get output devices

    // Populate input device dropdown
    for (int i = 0; i < inputDevices.size(); ++i) {
        inputDeviceComboBox_.addItem(inputDevices[i], i + 1);
    }

    // Populate output device dropdown
    for (int i = 0; i < outputDevices.size(); ++i) {
        outputDeviceComboBox_.addItem(outputDevices[i], i + 1);
    }

    // Select current devices
    auto setup = deviceManager_->getAudioDeviceSetup();

    int inputIndex = inputDevices.indexOf(setup.inputDeviceName);
    if (inputIndex >= 0) {
        inputDeviceComboBox_.setSelectedId(inputIndex + 1, juce::dontSendNotification);
    }

    int outputIndex = outputDevices.indexOf(setup.outputDeviceName);
    if (outputIndex >= 0) {
        outputDeviceComboBox_.setSelectedId(outputIndex + 1, juce::dontSendNotification);
    }
}

void AudioSettingsDialog::onInputDeviceSelected() {
    int selectedId = inputDeviceComboBox_.getSelectedId();
    if (selectedId == 0)
        return;

    juce::String selectedDeviceName = inputDeviceComboBox_.getItemText(selectedId - 1);

    // Get current setup
    auto setup = deviceManager_->getAudioDeviceSetup();

    // Change input device
    setup.inputDeviceName = selectedDeviceName;

    // Apply new device setup
    auto result = deviceManager_->setAudioDeviceSetup(setup, true);
    if (!result.isEmpty()) {
        DBG("Failed to switch input device: " << result);
        return;
    }

    enableAllChannelsOnCurrentDevice();

    // Update channel selectors to reflect new device
    inputChannelSelector_->updateFromDevice();

    // Update device name label
    if (auto* device = deviceManager_->getCurrentAudioDevice()) {
        juce::String labelText = "Input: " + setup.inputDeviceName;
        labelText += " | Output: " + setup.outputDeviceName;
        deviceNameLabel_.setText(labelText, juce::dontSendNotification);
    }
}

void AudioSettingsDialog::onOutputDeviceSelected() {
    int selectedId = outputDeviceComboBox_.getSelectedId();
    if (selectedId == 0)
        return;

    juce::String selectedDeviceName = outputDeviceComboBox_.getItemText(selectedId - 1);

    // Get current setup
    auto setup = deviceManager_->getAudioDeviceSetup();

    // Change output device
    setup.outputDeviceName = selectedDeviceName;

    // Apply new device setup
    auto result = deviceManager_->setAudioDeviceSetup(setup, true);
    if (!result.isEmpty()) {
        DBG("Failed to switch output device: " << result);
        return;
    }

    enableAllChannelsOnCurrentDevice();

    // Update channel selectors to reflect new device
    outputChannelSelector_->updateFromDevice();

    // Update device name label
    if (auto* device = deviceManager_->getCurrentAudioDevice()) {
        juce::String labelText = "Input: " + setup.inputDeviceName;
        labelText += " | Output: " + setup.outputDeviceName;
        deviceNameLabel_.setText(labelText, juce::dontSendNotification);
    }
}

void AudioSettingsDialog::enableAllChannelsOnCurrentDevice() {
    // Flush pending async updates so wave device list rebuilds before audio callback fires (#719)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(0);

    // Enable all channels on the current device — it may have a different channel count
    if (auto* device = deviceManager_->getCurrentAudioDevice()) {
        auto newSetup = deviceManager_->getAudioDeviceSetup();
        newSetup.inputChannels.setRange(0, device->getInputChannelNames().size(), true);
        newSetup.outputChannels.setRange(0, device->getOutputChannelNames().size(), true);
        deviceManager_->setAudioDeviceSetup(newSetup, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
    }
}

void AudioSettingsDialog::savePreferencesIfNeeded() {
    if (!setAsPreferredCheckbox_.getToggleState())
        return;

    auto setup = deviceManager_->getAudioDeviceSetup();

    // Count enabled channels from TE devices (JUCE bits are always all-on)
    int inputChannelCount = 0;
    int outputChannelCount = 0;
    if (teDeviceManager_) {
        for (auto* dev : teDeviceManager_->getWaveInputDevices()) {
            if (dev->isEnabled()) {
                for (const auto& ch : dev->getChannels())
                    inputChannelCount = std::max(inputChannelCount, ch.indexInDevice + 1);
            }
        }
        for (auto* dev : teDeviceManager_->getWaveOutputDevices()) {
            if (dev->isEnabled()) {
                for (const auto& ch : dev->getChannels())
                    outputChannelCount = std::max(outputChannelCount, ch.indexInDevice + 1);
            }
        }
    } else {
        // Fallback: count from JUCE setup
        for (int i = 0; i < setup.inputChannels.getHighestBit() + 1; ++i) {
            if (setup.inputChannels[i])
                inputChannelCount = i + 1;
        }
        for (int i = 0; i < setup.outputChannels.getHighestBit() + 1; ++i) {
            if (setup.outputChannels[i])
                outputChannelCount = i + 1;
        }
    }

    // Save to Config
    auto& config = magda::Config::getInstance();
    config.setPreferredInputDevice(setup.inputDeviceName.toStdString());
    config.setPreferredOutputDevice(setup.outputDeviceName.toStdString());
    config.setPreferredInputChannels(inputChannelCount);
    config.setPreferredOutputChannels(outputChannelCount);

    DBG("Saved preferred devices: Input=" << setup.inputDeviceName << " (" << inputChannelCount
                                          << " ch), Output=" << setup.outputDeviceName << " ("
                                          << outputChannelCount << " ch)");
}

void AudioSettingsDialog::showDialog(juce::Component* parent,
                                     juce::AudioDeviceManager* deviceManager,
                                     tracktion::DeviceManager* teDeviceManager) {
    if (deviceManager == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Audio Settings",
            "Audio engine not initialized. Cannot open audio settings.");
        return;
    }

    auto* dialog = new AudioSettingsDialog(deviceManager, teDeviceManager);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio/MIDI Settings";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

}  // namespace magda
