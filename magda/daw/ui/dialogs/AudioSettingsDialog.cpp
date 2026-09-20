#include "AudioSettingsDialog.hpp"

#include <cstdlib>

#include "../../audio/AudioDriverUtils.hpp"
#include "../../audio/MidiBridge.hpp"
#include "../../audio/io/AudioIOControl.hpp"
#include "../../core/Config.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/AudioEngineChoice.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "../utils/ChannelLabels.hpp"

namespace magda {

namespace {

/// A JUCE item id of zero means "nothing selected", so the engine ids are the
/// enum shifted by one rather than the enum itself (#2559).
int engineItemId(AudioEngineChoice choice) {
    return static_cast<int>(choice) + 1;
}

AudioEngineChoice engineForItemId(int itemId) {
    return itemId == engineItemId(AudioEngineChoice::Magda) ? AudioEngineChoice::Magda
                                                            : AudioEngineChoice::Tracktion;
}

}  // namespace

namespace {

bool comboItemsMatchDriverTypes(const juce::ComboBox& comboBox,
                                juce::AudioDeviceManager& deviceManager) {
    const auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
    if (comboBox.getNumItems() != deviceTypes.size())
        return false;

    for (int i = 0; i < deviceTypes.size(); ++i) {
        auto* type = deviceTypes.getUnchecked(i);
        if (type == nullptr || comboBox.getItemText(i) != type->getTypeName())
            return false;
    }

    return true;
}

juce::ComboBox* findDriverTypeComboBox(juce::Component& root,
                                       juce::AudioDeviceManager& deviceManager) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        auto* child = root.getChildComponent(i);
        if (child == nullptr)
            continue;

        if (auto* comboBox = dynamic_cast<juce::ComboBox*>(child);
            comboBox != nullptr && comboItemsMatchDriverTypes(*comboBox, deviceManager)) {
            return comboBox;
        }

        if (auto* nested = findDriverTypeComboBox(*child, deviceManager))
            return nested;
    }

    return nullptr;
}

}  // namespace

// ============================================================================
// CustomChannelSelector Implementation
// ============================================================================

namespace {

constexpr int kToggleHeight = 24;
constexpr int kRowSpacing = 4;

}  // namespace

CustomChannelSelector::CustomChannelSelector(AudioIOControl& audio, bool isInput)
    : audio_(audio), isInput_(isInput) {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());
    titleLabel_.setText(isInput ? "Audio Inputs:" : "Audio Outputs:", juce::dontSendNotification);
    titleLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(titleLabel_);

    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    refresh();
}

CustomChannelSelector::~CustomChannelSelector() {
    setLookAndFeel(nullptr);
}

void CustomChannelSelector::refresh() {
    channelToggles_.clear();

    const auto chosen = audio_.chosen();
    const juce::String interfaceName = isInput_ ? chosen.inputInterface : chosen.outputInterface;
    const auto channelNames = audio_.channelNames(chosen.backend, interfaceName, isInput_);

    juce::BigInteger activeChannels;
    for (const auto channel : isInput_ ? chosen.inputChannels : chosen.outputChannels)
        activeChannels.setBit(channel);

    int numChannels = channelNames.size();

    // Read current preview output channel from Config (only relevant for output)
    int previewOffset = magda::Config::getInstance().getPreviewOutputChannel();

    // Create stereo pair toggles first
    for (int i = 0; i < numChannels; i += 2) {
        if (i + 1 < numChannels) {
            ChannelToggle toggle;
            toggle.button =
                std::make_unique<juce::ToggleButton>(ChannelLabels::pair(channelNames, i, i + 1));
            toggle.button->setTooltip(toggle.button->getButtonText());
            toggle.startChannel = i;
            toggle.isStereo = true;

            // Check if both channels in pair are active
            bool pairActive = activeChannels[i] && activeChannels[i + 1];
            toggle.button->setToggleState(pairActive, juce::dontSendNotification);

            toggle.button->onClick = [this, i]() { onChannelToggled(i, true); };
            list_.addAndMakeVisible(*toggle.button);

            // For output channels, add a "Preview" toggle next to each stereo pair
            if (!isInput_) {
                toggle.previewButton = std::make_unique<juce::ToggleButton>("Preview");
                toggle.previewButton->setToggleState(i == previewOffset,
                                                     juce::dontSendNotification);
                toggle.previewButton->setRadioGroupId(9999);  // Mutual exclusion
                toggle.previewButton->onClick = [button = toggle.previewButton.get(), i]() {
                    // Prevent unchecking — always keep one preview destination selected
                    if (!button->getToggleState()) {
                        button->setToggleState(true, juce::dontSendNotification);
                        return;
                    }
                    onPreviewToggled(i);
                };
                list_.addAndMakeVisible(*toggle.previewButton);
            }

            channelToggles_.push_back(std::move(toggle));
        }
    }

    // Create individual mono channel toggles
    for (int i = 0; i < numChannels; ++i) {
        ChannelToggle toggle;
        toggle.button = std::make_unique<juce::ToggleButton>(ChannelLabels::mono(channelNames, i));
        toggle.button->setTooltip(toggle.button->getButtonText());
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
        list_.addAndMakeVisible(*toggle.button);
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
    applyTicks();
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

void CustomChannelSelector::onPreviewToggled(int startChannel) {
    magda::Config::getInstance().setPreviewOutputChannel(startChannel);
    magda::Config::getInstance().save();
    DBG("Preview output changed to channels " << (startChannel + 1) << "-" << (startChannel + 2));
}

void CustomChannelSelector::applyTicks() {
    auto settings = audio_.chosen();
    auto& channels = isInput_ ? settings.inputChannels : settings.outputChannels;
    channels.clear();
    for (const auto& toggle : channelToggles_) {
        if (!toggle.button->getToggleState())
            continue;
        channels.push_back(toggle.startChannel);
        if (toggle.isStereo)
            channels.push_back(toggle.startChannel + 1);
    }
    std::ranges::sort(channels);
    audio_.apply(settings);
}

void CustomChannelSelector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
}

int CustomChannelSelector::rowsHeight() const {
    return static_cast<int>(channelToggles_.size()) * (kToggleHeight + kRowSpacing);
}

void CustomChannelSelector::layOutRows(int width) {
    constexpr auto toggleHeight = kToggleHeight;

    auto top = 0;
    for (auto& toggle : channelToggles_) {
        auto row = juce::Rectangle<int>(0, top, std::max(0, width), toggleHeight);
        if (toggle.previewButton != nullptr) {
            // Measured rather than left at a round number, which is what cut the
            // label off: the tick is drawn at the row's height and the text
            // follows it, so both have to be paid for.
            const auto text = juce::GlyphArrangement::getStringWidthInt(
                FontManager::getInstance().getUIFont(static_cast<float>(toggleHeight) * 0.6f),
                "Preview");
            toggle.previewButton->setBounds(row.removeFromRight(text + toggleHeight + 8));
            row.removeFromRight(4);
        }
        toggle.button->setBounds(row);
        top += toggleHeight + kRowSpacing;
    }
}

void CustomChannelSelector::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    viewport_.setBounds(bounds);

    // Height first, then width, and in that order for a reason: sizing the list
    // is what brings the scrollbar in, and the scrollbar takes width away from
    // the rows. Measuring first lays them out for a column that is about to get
    // narrower, which puts the Preview control underneath it on any device with
    // more channels than the column can show.
    //
    // Two sizes rather than a loop because the height does not depend on the
    // width: the first settles whether there is a scrollbar at all, so the
    // width read after it is final.
    const auto height = rowsHeight();
    list_.setSize(bounds.getWidth(), height);

    const auto width = std::max(0, viewport_.getMaximumVisibleWidth());
    list_.setSize(width, height);
    layOutRows(width);
}

// ============================================================================
// AudioSettingsDialog Implementation
// ============================================================================

AudioSettingsDialog::AudioSettingsDialog(AudioEngine* audioEngine)
    : deviceRefreshSpinner_(deviceRefreshProgress_),
      deviceManager_(audioEngine != nullptr ? audioEngine->getDeviceManager() : nullptr),
      audioEngine_(audioEngine),
      audio_(audioEngine != nullptr ? audioEngine->getAudioIO() : nullptr) {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // Input device selection dropdown
    inputDeviceLabel_.setText("Input Interface:", juce::dontSendNotification);
    inputDeviceLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(inputDeviceLabel_);

    inputDeviceComboBox_.onChange = [this]() { onInputDeviceSelected(); };
    addAndMakeVisible(inputDeviceComboBox_);

    // Output device selection dropdown
    outputDeviceLabel_.setText("Output Interface:", juce::dontSendNotification);
    outputDeviceLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(outputDeviceLabel_);

    outputDeviceComboBox_.onChange = [this]() { onOutputDeviceSelected(); };
    addAndMakeVisible(outputDeviceComboBox_);

    deviceRefreshSpinner_.setStyle(juce::ProgressBar::Style::circular);
    deviceRefreshSpinner_.setPercentageDisplay(false);
    deviceRefreshSpinner_.setColour(juce::ProgressBar::backgroundColourId,
                                    juce::Colours::white.withAlpha(0.12f));
    deviceRefreshSpinner_.setColour(juce::ProgressBar::foregroundColourId,
                                    juce::Colour(0xff4a90d9));
    deviceRefreshSpinner_.setTooltip("Refreshing audio devices");
    addAndMakeVisible(deviceRefreshSpinner_);
    deviceRefreshSpinner_.setVisible(false);

    deviceRefreshLabel_.setText("Refreshing devices...", juce::dontSendNotification);
    deviceRefreshLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    deviceRefreshLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    deviceRefreshLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(deviceRefreshLabel_);
    deviceRefreshLabel_.setVisible(false);

    populateDeviceLists();

    // "Set as preferred devices" checkbox
    setAsPreferredCheckbox_.setButtonText("Set as preferred devices (auto-select on startup)");
    addAndMakeVisible(setAsPreferredCheckbox_);

    // Check if current devices match preferred devices in Config
    auto& config = magda::Config::getInstance();
    auto setup = deviceManager_->getAudioDeviceSetup();
    bool inputMatches = setup.inputDeviceName.toStdString() == config.getPreferredInputDevice();
    bool outputMatches = setup.outputDeviceName.toStdString() == config.getPreferredOutputDevice();
    setAsPreferredCheckbox_.setToggleState(inputMatches && outputMatches,
                                           juce::dontSendNotification);

    // Which engine renders. An audio-device-level choice, so it sits with the
    // devices rather than in a preferences pane of its own (#2559).
    engineLabel_.setText("Audio Engine:", juce::dontSendNotification);
    engineLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(engineLabel_);

    engineComboBox_.addItem("Tracktion Engine", engineItemId(AudioEngineChoice::Tracktion));
    engineComboBox_.addItem("MAGDA Engine (beta)", engineItemId(AudioEngineChoice::Magda));
    engineComboBox_.setSelectedId(
        engineItemId(
            parseAudioEngine(config.getAudioEngine()).value_or(AudioEngineChoice::Tracktion)),
        juce::dontSendNotification);
    engineComboBox_.onChange = [this]() { onAudioEngineSelected(); };
    addAndMakeVisible(engineComboBox_);

    // Said rather than implied. The engine is chosen once on the way up, and
    // swapping one under a loaded project with open plugin editors is not a
    // thing to do quietly for a setting used twice.
    const auto* engineOverride = std::getenv("MAGDA_AUDIO_ENGINE");
    engineRestartLabel_.setText(engineOverride != nullptr
                                    ? "MAGDA_AUDIO_ENGINE overrides this for the current run"
                                    : "Takes effect after a restart",
                                juce::dontSendNotification);
    engineRestartLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    engineRestartLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    addAndMakeVisible(engineRestartLabel_);

    activeMidiInputs_ = std::make_unique<ActiveMidiInputs>(*deviceManager_);

    // Create the device selector component (MIDI only, no audio device selection)
    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        *deviceManager_,
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
    attachDriverTypeComboListener();

    // Refresh the device combos whenever the driver type / device changes via the
    // selector above, so they keep listing devices for the active driver.
    deviceManager_->addChangeListener(this);

    // Create custom channel selectors for inputs and outputs
    inputChannelSelector_ = std::make_unique<CustomChannelSelector>(*audio_, true);
    addAndMakeVisible(*inputChannelSelector_);

    outputChannelSelector_ = std::make_unique<CustomChannelSelector>(*audio_, false);
    addAndMakeVisible(*outputChannelSelector_);

    deviceNameLabel_.setFont(FontManager::getInstance().getUIFontBold(16.0f));
    deviceNameLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(deviceNameLabel_);
    refreshChosenInterface();

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

AudioSettingsDialog::~AudioSettingsDialog() {
    detachDriverTypeComboListener();
    deviceManager_->removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void AudioSettingsDialog::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) {
    if (comboBoxThatHasChanged == driverTypeComboBox_)
        showDeviceRefreshIndicator(true);
}

void AudioSettingsDialog::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source != deviceManager_)
        return;

    if (activeMidiInputs_->saveChanges() && audioEngine_ != nullptr)
        if (auto* midi = audioEngine_->getMidiBridge())
            midi->activeInputsChanged();

    // The JUCE selector changes the backend, rate and block size on the manager itself.
    keepSelectorChanges();

    // A channel toggle reopens the interface too, and its lists are already right.
    const auto chosen = audio_->chosen();
    if (chosen.backend == listed_.backend && chosen.inputInterface == listed_.inputInterface &&
        chosen.outputInterface == listed_.outputInterface) {
        showOpenInterface();
        hideDeviceRefreshIndicator();
        return;
    }

    showDeviceRefreshIndicator(true);
    refreshChosenInterface();
    hideDeviceRefreshIndicator();
}

void AudioSettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void AudioSettingsDialog::lookAndFeelChanged() {
    refreshHostWindowBackground(*this);
}

void AudioSettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Device name label at top
    auto deviceNameArea = bounds.removeFromTop(30);
    if (deviceRefreshSpinner_.isVisible()) {
        auto refreshArea = deviceNameArea.removeFromRight(170);
        deviceRefreshSpinner_.setBounds(
            refreshArea.removeFromRight(22).withSizeKeepingCentre(18, 18));
        refreshArea.removeFromRight(6);
        deviceRefreshLabel_.setBounds(refreshArea);
    } else {
        deviceRefreshSpinner_.setBounds({});
        deviceRefreshLabel_.setBounds({});
    }
    deviceNameLabel_.setBounds(deviceNameArea);
    bounds.removeFromTop(10);  // spacing

    // Input device selection dropdown
    auto inputDeviceArea = bounds.removeFromTop(28);
    inputDeviceLabel_.setBounds(inputDeviceArea.removeFromLeft(120));
    inputDeviceArea.removeFromLeft(10);  // spacing
    inputDeviceComboBox_.setBounds(inputDeviceArea);
    bounds.removeFromTop(5);  // spacing

    if (outputDeviceComboBox_.isVisible()) {
        // Output device selection dropdown
        auto outputDeviceArea = bounds.removeFromTop(28);
        outputDeviceLabel_.setBounds(outputDeviceArea.removeFromLeft(120));
        outputDeviceArea.removeFromLeft(10);  // spacing
        outputDeviceComboBox_.setBounds(outputDeviceArea);
        bounds.removeFromTop(5);  // spacing
    }

    // "Set as preferred" checkbox
    setAsPreferredCheckbox_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(5);  // spacing

    // Engine choice, with its restart note beside it rather than under it
    auto engineArea = bounds.removeFromTop(28);
    engineLabel_.setBounds(engineArea.removeFromLeft(120));
    engineArea.removeFromLeft(10);  // spacing
    engineComboBox_.setBounds(engineArea.removeFromLeft(220));
    engineArea.removeFromLeft(10);  // spacing
    engineRestartLabel_.setBounds(engineArea);
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
    // clear() defaults to sendNotificationAsync: on a re-list (every device
    // change notification lands here) the queued onChange fires after the
    // current device is re-selected below and re-applies the device setup,
    // which broadcasts another change. The selection below is explicit, so
    // nothing here wants that notification.
    inputDeviceComboBox_.clear(juce::dontSendNotification);
    outputDeviceComboBox_.clear(juce::dontSendNotification);
    updateDevicePickerMode();

    // List devices from the ACTIVE driver type (see activeDeviceTypeFor): using
    // getAvailableDeviceTypes()[0] listed the wrong driver's devices once a
    // non-first driver was selected, failing with "No such device".
    auto* deviceType = activeDeviceTypeFor(*deviceManager_);
    if (deviceType == nullptr)
        return;
    deviceType->scanForDevices();

    const bool singleDeviceDriver = isSingleDeviceDriver(*deviceManager_);
    auto inputDevices =
        singleDeviceDriver ? deviceType->getDeviceNames() : deviceType->getDeviceNames(true);
    auto outputDevices =
        singleDeviceDriver ? juce::StringArray() : deviceType->getDeviceNames(false);

    // Populate input device dropdown
    for (int i = 0; i < inputDevices.size(); ++i) {
        inputDeviceComboBox_.addItem(inputDevices[i], i + 1);
    }

    if (!singleDeviceDriver) {
        // Populate output device dropdown
        for (int i = 0; i < outputDevices.size(); ++i) {
            outputDeviceComboBox_.addItem(outputDevices[i], i + 1);
        }
    }

    // The chosen interfaces, which include one chosen with no channels open on it.
    const auto chosen = audio_->chosen();

    int inputIndex = inputDevices.indexOf(juce::String(chosen.inputInterface));
    if (singleDeviceDriver && inputIndex < 0)
        inputIndex = inputDevices.indexOf(juce::String(chosen.outputInterface));
    if (inputIndex >= 0) {
        inputDeviceComboBox_.setSelectedId(inputIndex + 1, juce::dontSendNotification);
    }

    if (!singleDeviceDriver) {
        int outputIndex = outputDevices.indexOf(juce::String(chosen.outputInterface));
        if (outputIndex >= 0) {
            outputDeviceComboBox_.setSelectedId(outputIndex + 1, juce::dontSendNotification);
        }
    }
}

void AudioSettingsDialog::updateDevicePickerMode() {
    const bool singleDeviceDriver = isSingleDeviceDriver(*deviceManager_);

    inputDeviceLabel_.setText(singleDeviceDriver ? "Interface:" : "Input Interface:",
                              juce::dontSendNotification);
    outputDeviceLabel_.setVisible(!singleDeviceDriver);
    outputDeviceComboBox_.setVisible(!singleDeviceDriver);
}

void AudioSettingsDialog::attachDriverTypeComboListener() {
    detachDriverTypeComboListener();

    if (deviceSelector_ == nullptr)
        return;

    driverTypeComboBox_ = findDriverTypeComboBox(*deviceSelector_, *deviceManager_);
    if (driverTypeComboBox_ != nullptr)
        driverTypeComboBox_->addListener(this);
}

void AudioSettingsDialog::detachDriverTypeComboListener() {
    if (driverTypeComboBox_ != nullptr) {
        driverTypeComboBox_->removeListener(this);
        driverTypeComboBox_ = nullptr;
    }
}

void AudioSettingsDialog::showDeviceRefreshIndicator(bool flushRepaint) {
    if (!deviceRefreshSpinner_.isVisible()) {
        deviceRefreshSpinner_.setVisible(true);
        deviceRefreshLabel_.setVisible(true);
        resized();
    }

    deviceRefreshSpinner_.toFront(false);
    deviceRefreshLabel_.toFront(false);
    repaint();

    if (flushRepaint)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

void AudioSettingsDialog::hideDeviceRefreshIndicator() {
    if (!deviceRefreshSpinner_.isVisible())
        return;
    deviceRefreshSpinner_.setVisible(false);
    deviceRefreshLabel_.setVisible(false);
    resized();
}

void AudioSettingsDialog::onInputDeviceSelected() {
    if (const auto id = inputDeviceComboBox_.getSelectedId(); id > 0)
        chooseInterface(inputDeviceComboBox_.getItemText(id - 1), true);
}

void AudioSettingsDialog::onOutputDeviceSelected() {
    if (isSingleDeviceDriver(*deviceManager_))
        return;
    if (const auto id = outputDeviceComboBox_.getSelectedId(); id > 0)
        chooseInterface(outputDeviceComboBox_.getItemText(id - 1), false);
}

namespace {

/**
 * @brief Keep the chosen channels that @p settings' interfaces have, falling back to stereo out.
 *
 * Nothing is opened because an interface advertises it (#2528); @p outputsNew says the output
 * interface changed, and only then does an empty output selection become stereo.
 */
void keepChannelsTheyHave(AudioIOControl& audio, AudioIOSettings& settings, bool outputsNew) {
    const auto keep = [&](std::vector<int>& channels, const std::string& interfaceName,
                          bool inputs) {
        const auto count = audio.channelNames(settings.backend, interfaceName, inputs).size();
        std::erase_if(channels, [count](int channel) { return channel >= count; });
    };
    keep(settings.inputChannels, settings.inputInterface, true);
    keep(settings.outputChannels, settings.outputInterface, false);
    if (outputsNew && settings.outputChannels.empty())
        settings.outputChannels = {0, 1};
}

}  // namespace

void AudioSettingsDialog::chooseInterface(const juce::String& interfaceName, bool inputs) {
    const auto single = isSingleDeviceDriver(*deviceManager_);
    auto settings = audio_->chosen();
    if (single || inputs)
        settings.inputInterface = interfaceName.toStdString();
    if (single || !inputs)
        settings.outputInterface = interfaceName.toStdString();
    keepChannelsTheyHave(*audio_, settings, single || !inputs);

    showDeviceRefreshIndicator(true);
    auto error = audio_->apply(settings);

    // CoreAudio cannot pair some interfaces (no shared sample rate), so use this one both ways.
    if (error.isNotEmpty() && !single && settings.inputInterface != settings.outputInterface) {
        settings.inputInterface = settings.outputInterface = interfaceName.toStdString();
        keepChannelsTheyHave(*audio_, settings, true);
        error = audio_->apply(settings);
    }

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Audio Interface Error",
            "Could not open \"" + interfaceName + "\".\n\nError: " + error);

    refreshChosenInterface();
    hideDeviceRefreshIndicator();
}

void AudioSettingsDialog::keepSelectorChanges() {
    auto* device = deviceManager_->getCurrentAudioDevice();
    if (device == nullptr)
        return;

    auto settings = audio_->chosen();
    const auto backend = device->getTypeName().toStdString();
    const auto rate = device->getCurrentSampleRate();
    const auto blockSize = device->getCurrentBufferSizeSamples();
    if (settings.backend == backend && settings.sampleRate == rate &&
        settings.bufferSize == blockSize)
        return;

    if (settings.backend != backend) {
        // The selector opened the new backend's default interface; the choice follows it.
        const auto setup = deviceManager_->getAudioDeviceSetup();
        settings.backend = backend;
        settings.inputInterface = setup.inputDeviceName.toStdString();
        settings.outputInterface = setup.outputDeviceName.toStdString();
        keepChannelsTheyHave(*audio_, settings, true);
    }
    settings.sampleRate = rate;
    settings.bufferSize = blockSize;
    audio_->apply(settings);
}

void AudioSettingsDialog::refreshChosenInterface() {
    listed_ = audio_->chosen();
    populateDeviceLists();
    inputChannelSelector_->refresh();
    outputChannelSelector_->refresh();
    showOpenInterface();
    resized();
}

void AudioSettingsDialog::showOpenInterface() {
    if (auto* device = deviceManager_->getCurrentAudioDevice()) {
        deviceNameLabel_.setText("Current Interface: " + device->getName() + " (" +
                                     juce::String(device->getInputChannelNames().size()) + " in, " +
                                     juce::String(device->getOutputChannelNames().size()) + " out)",
                                 juce::dontSendNotification);
    } else {
        deviceNameLabel_.setText("No audio interface open", juce::dontSendNotification);
    }
}

void AudioSettingsDialog::savePreferencesIfNeeded() {
    if (!setAsPreferredCheckbox_.getToggleState())
        return;

    auto setup = deviceManager_->getAudioDeviceSetup();

    // Count enabled channels from the engine's wave-device state.
    int inputChannelCount = 0;
    int outputChannelCount = 0;
    if (auto* hardware = audioEngine_ != nullptr ? audioEngine_->getAudioIO() : nullptr) {
        inputChannelCount = hardware->inputs().open.getHighestBit() + 1;
        outputChannelCount = hardware->outputs().open.getHighestBit() + 1;
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
    juce::String preferredInputDevice = setup.inputDeviceName;
    juce::String preferredOutputDevice = setup.outputDeviceName;
    if (isSingleDeviceDriver(*deviceManager_)) {
        preferredInputDevice =
            setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : setup.outputDeviceName;
        preferredOutputDevice = preferredInputDevice;
    }
    config.setPreferredInputDevice(preferredInputDevice.toStdString());
    config.setPreferredOutputDevice(preferredOutputDevice.toStdString());
    config.setPreferredInputChannels(inputChannelCount);
    config.setPreferredOutputChannels(outputChannelCount);

    DBG("Saved preferred devices: Input=" << preferredInputDevice << " (" << inputChannelCount
                                          << " ch), Output=" << preferredOutputDevice << " ("
                                          << outputChannelCount << " ch)");
}

void AudioSettingsDialog::onAudioEngineSelected() {
    auto& config = magda::Config::getInstance();
    config.setAudioEngine(settingWordFor(engineForItemId(engineComboBox_.getSelectedId())));
    config.save();
}

void AudioSettingsDialog::showDialog(juce::Component* parent, AudioEngine* audioEngine) {
    juce::ignoreUnused(parent);
    if (audioEngine == nullptr || audioEngine->getDeviceManager() == nullptr ||
        audioEngine->getAudioIO() == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Audio Settings",
            "Audio engine not initialized. Cannot open audio settings.");
        return;
    }

    auto* dialog = new AudioSettingsDialog(audioEngine);

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
