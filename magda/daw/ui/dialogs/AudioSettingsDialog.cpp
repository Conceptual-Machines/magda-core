#include "AudioSettingsDialog.hpp"

#include "../../audio/AudioDriverUtils.hpp"
#include "../../audio/MidiBridge.hpp"
#include "../../audio/io/AudioIOControl.hpp"
#include "../../core/Config.hpp"
#include "../../core/StringTable.hpp"
#include "../../core/TrackManager.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../themes/ActiveTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "../utils/ChannelLabels.hpp"

namespace magda {

// ============================================================================
// CustomChannelSelector Implementation
// ============================================================================

namespace {

constexpr int kToggleHeight = 24;
constexpr int kRowSpacing = 4;
/// The tick DialogLookAndFeel draws, and the gap it leaves before a toggle's text.
constexpr int kTickWidth = 31;

/// Debug builds only: a click-by-click trace has no place in a user's log.
void log([[maybe_unused]] const juce::String& message) {
#if JUCE_DEBUG
    juce::Logger::writeToLog("[audio-settings] " + message);
#endif
}

/// One-based, as the rows show them.
juce::String channelList(const std::vector<int>& channels) {
    juce::StringArray numbers;
    for (const auto channel : channels)
        numbers.add(juce::String(channel + 1));
    return "[" + numbers.joinIntoString(",") + "]";
}

juce::String channelList(const juce::BigInteger& mask) {
    std::vector<int> channels;
    for (auto bit = mask.findNextSetBit(0); bit >= 0; bit = mask.findNextSetBit(bit + 1))
        channels.push_back(bit);
    return channelList(channels);
}

/// A channel's name, shown with its socket and edited as the user's name alone.
class ChannelNameLabel final : public juce::Label {
  public:
    std::function<juce::String()> userName;
    std::function<void(const juce::String&)> onRename;
    std::function<void()> onClear;

  protected:
    void editorShown(juce::TextEditor* editor) override {
        editor->setText(userName(), false);
        editor->selectAll();
    }

    // Committed against the user's name, not the label shown, so typing the label itself
    // still names the channel.
    void editorAboutToBeHidden(juce::TextEditor* editor) override {
        if (!cancelled_ && editor->getText().trim() != userName())
            onRename(editor->getText());
        editor->setText(getText(), false);
    }

    void textEditorEscapeKeyPressed(juce::TextEditor& editor) override {
        cancelled_ = true;
        juce::Label::textEditorEscapeKeyPressed(editor);
        cancelled_ = false;
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) {
            juce::Label::mouseDown(e);
            return;
        }
        using Safe = juce::Component::SafePointer<ChannelNameLabel>;
        juce::PopupMenu menu;
        menu.addItem(tr("audio_settings.menu.rename_channel"), [safe = Safe(this)] {
            if (safe != nullptr)
                safe->showEditor();
        });
        menu.addItem(tr("audio_settings.menu.clear_channel_name"), userName().isNotEmpty(), false,
                     [safe = Safe(this)] {
                         if (safe != nullptr)
                             safe->onClear();
                     });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

  private:
    bool cancelled_ = false;
};

}  // namespace

CustomChannelSelector::CustomChannelSelector(AudioIOControl& audio, bool isInput)
    : audio_(audio), isInput_(isInput) {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());
    titleLabel_.setText(isInput ? tr("audio_settings.label.audio_inputs")
                                : tr("audio_settings.label.audio_outputs"),
                        juce::dontSendNotification);
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
    interfaceName_ = isInput_ ? chosen.inputInterface : chosen.outputInterface;
    driverNames_ = audio_.channelNames(chosen.backend, interfaceName_, isInput_);

    juce::BigInteger activeChannels;
    for (const auto channel : isInput_ ? chosen.inputChannels : chosen.outputChannels)
        activeChannels.setBit(channel);
    juce::BigInteger monoChannels;
    for (const auto channel : isInput_ ? chosen.inputMonoChannels : chosen.outputMonoChannels)
        monoChannels.setBit(channel);

    int numChannels = driverNames_.size();

    // Read current preview output channel from Config (only relevant for output)
    int previewOffset = magda::Config::getInstance().getPreviewOutputChannel();

    // Create stereo pair toggles first
    for (int i = 0; i < numChannels; i += 2) {
        if (i + 1 < numChannels) {
            ChannelToggle toggle;
            toggle.button = std::make_unique<juce::ToggleButton>();
            toggle.name = makeNameLabel(i, true);
            toggle.startChannel = i;
            toggle.isStereo = true;

            // Check if both channels in pair are active
            bool pairActive = activeChannels[i] && activeChannels[i + 1] && !monoChannels[i] &&
                              !monoChannels[i + 1];
            toggle.button->setToggleState(pairActive, juce::dontSendNotification);

            toggle.button->onClick = [this, i]() { onChannelToggled(i, true); };
            list_.addAndMakeVisible(*toggle.button);
            list_.addAndMakeVisible(*toggle.name);

            // For output channels, add a "Preview" toggle next to each stereo pair
            if (!isInput_) {
                toggle.previewButton =
                    std::make_unique<juce::ToggleButton>(tr("audio_settings.toggle.preview"));
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
        toggle.button = std::make_unique<juce::ToggleButton>();
        toggle.name = makeNameLabel(i, false);
        toggle.startChannel = i;
        toggle.isStereo = false;

        // Open on its own, or its pair was ticked as two monos
        const auto partner = i % 2 == 0 ? i + 1 : i - 1;
        const auto hasPartner = partner < numChannels;
        const bool monoActive = activeChannels[i] && (!hasPartner || !activeChannels[partner] ||
                                                      monoChannels[i] || monoChannels[partner]);

        toggle.button->setToggleState(monoActive, juce::dontSendNotification);
        toggle.button->onClick = [this, i]() { onChannelToggled(i, false); };
        list_.addAndMakeVisible(*toggle.button);
        list_.addAndMakeVisible(*toggle.name);
        channelToggles_.push_back(std::move(toggle));
    }

    refreshChannelStates();
    resized();
}

void CustomChannelSelector::onChannelToggled(int channelIndex, bool isStereo) {
    for (const auto& toggle : channelToggles_)
        if (toggle.startChannel == channelIndex && toggle.isStereo == isStereo)
            log(juce::String(isInput_ ? "input " : "output ") +
                (isStereo ? juce::String(channelIndex + 1) + "-" + juce::String(channelIndex + 2)
                          : juce::String(channelIndex + 1) + " mono") +
                (toggle.button->getToggleState() ? " ticked" : " unticked"));
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

juce::String CustomChannelSelector::rowLabel(int startChannel, bool isStereo) const {
    const auto& user =
        Config::getInstance().getChannelNames(interfaceName_.toStdString(), isInput_);
    return isStereo ? ChannelLabels::pair(driverNames_, startChannel, startChannel + 1, user)
                    : ChannelLabels::mono(driverNames_, startChannel, user);
}

std::unique_ptr<juce::Label> CustomChannelSelector::makeNameLabel(int startChannel, bool isStereo) {
    auto label = std::make_unique<ChannelNameLabel>();
    label->setText(rowLabel(startChannel, isStereo), juce::dontSendNotification);
    label->setTooltip(label->getText() + "\n" + tr("audio_settings.tooltip.rename_channel"));
    label->setFont(FontManager::getInstance().getUIFont(15.0f));
    label->setBorderSize({});
    label->setEditable(false, true);
    label->setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    label->setColour(juce::Label::textWhenEditingColourId, ActiveTheme::getTextColour());
    label->setColour(juce::Label::backgroundWhenEditingColourId,
                     ActiveTheme::getColour(ActiveTheme::SURFACE));
    label->setColour(juce::Label::outlineWhenEditingColourId,
                     ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
    label->userName = [this, startChannel, isStereo] {
        const auto& user =
            Config::getInstance().getChannelNames(interfaceName_.toStdString(), isInput_);
        return juce::String::fromUTF8(user.nameOf(startChannel, isStereo).c_str());
    };
    label->onRename = [this, startChannel, isStereo](const juce::String& name) {
        rename(startChannel, isStereo, name);
    };
    label->onClear = [this, startChannel, isStereo] { rename(startChannel, isStereo, {}); };
    return label;
}

void CustomChannelSelector::rename(int startChannel, bool isStereo, const juce::String& name) {
    audio_.setChannelName(interfaceName_, isInput_, startChannel, isStereo, name);
    for (auto& toggle : channelToggles_) {
        if (toggle.startChannel != startChannel || toggle.isStereo != isStereo)
            continue;
        toggle.name->setText(rowLabel(startChannel, isStereo), juce::dontSendNotification);
        toggle.name->setTooltip(toggle.name->getText() + "\n" +
                                tr("audio_settings.tooltip.rename_channel"));
    }
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
    for (auto& toggle : channelToggles_) {
        toggle.name->setColour(juce::Label::textColourId,
                               toggle.button->getToggleState()
                                   ? ActiveTheme::getTextColour()
                                   : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        toggle.name->setAlpha(toggle.button->isEnabled() ? 1.0f : 0.5f);
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
    auto& mono = isInput_ ? settings.inputMonoChannels : settings.outputMonoChannels;
    channels.clear();
    mono.clear();
    for (const auto& toggle : channelToggles_) {
        if (!toggle.button->getToggleState())
            continue;
        channels.push_back(toggle.startChannel);
        if (toggle.isStereo)
            channels.push_back(toggle.startChannel + 1);
        else
            mono.push_back(toggle.startChannel);
    }
    std::ranges::sort(channels);
    const auto error = audio_.apply(settings);
    const auto open = isInput_ ? audio_.inputs().open : audio_.outputs().open;
    log(juce::String(isInput_ ? "inputs" : "outputs") + " asked " + channelList(channels) + " on " +
        (isInput_ ? settings.inputInterface : settings.outputInterface) + ", open " +
        channelList(open) + (error.isNotEmpty() ? ", error: " + error : juce::String()));
}

void CustomChannelSelector::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::SURFACE));
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
                tr("audio_settings.toggle.preview"));
            toggle.previewButton->setBounds(row.removeFromRight(text + toggleHeight + 8));
            row.removeFromRight(4);
        }
        toggle.button->setBounds(row.removeFromLeft(kTickWidth));
        toggle.name->setBounds(row);
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

// =============================================================================
// MidiInputList
// =============================================================================

MidiInputList::MidiInputList(AudioIOControl& audio) : audio_(audio) {
    viewport_.setViewedComponent(&rows_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    refresh();
}

void MidiInputList::resized() {
    viewport_.setBounds(getLocalBounds());

    const auto width = std::max(0, viewport_.getMaximumVisibleWidth());
    rows_.setSize(width, preferredHeight());

    auto top = 0;
    for (auto& toggle : toggles_) {
        toggle->setBounds(0, top, width, kToggleHeight);
        top += kToggleHeight + kRowSpacing;
    }
}

int MidiInputList::preferredHeight() const {
    // One row when there is nothing, so the section keeps its shape on a machine with no
    // MIDI at all rather than collapsing the label onto the button below it.
    return std::max<int>(1, toggles_.size()) * (kToggleHeight + kRowSpacing);
}

void MidiInputList::refresh() {
    devices_ = juce::MidiInput::getAvailableDevices();
    toggles_.clear();

    // Config is the choice; JUCE is told about it rather than asked (#2755).
    const auto& config = Config::getInstance();
    for (int i = 0; i < devices_.size(); ++i) {
        const auto& device = devices_[i];
        const auto active = config.isMidiInputActive(device.name);
        audio_.setMidiInputEnabled(device.identifier, active);

        // A real ToggleButton rather than a painted tick, so these read as the same
        // control as the audio channels beside them.
        auto button = std::make_unique<juce::ToggleButton>(device.name);
        button->setTooltip(device.name);
        button->setToggleState(active, juce::dontSendNotification);
        button->onClick = [this, i]() { toggle(i); };
        rows_.addAndMakeVisible(*button);
        toggles_.push_back(std::move(button));
    }

    // The row count decides the section's height, so the dialog repacks around it.
    if (auto* parent = getParentComponent())
        parent->resized();
    resized();
}

void MidiInputList::toggle(int index) {
    if (index < 0 || index >= devices_.size())
        return;

    const auto& device = devices_[index];
    auto& config = Config::getInstance();
    const auto active = toggles_[static_cast<std::size_t>(index)]->getToggleState();

    auto inactive = config.getInactiveMidiInputs();
    std::erase_if(inactive, [&device](const std::string& name) {
        return device.name.equalsIgnoreCase(juce::String(name));
    });
    if (!active)
        inactive.push_back(device.name.toStdString());
    config.setInactiveMidiInputs(std::move(inactive));
    config.save();

    audio_.setMidiInputEnabled(device.identifier, active);

    MidiBridge::getInstance().activeInputsChanged();
}

AudioSettingsDialog::AudioSettingsDialog(AudioEngine* audioEngine)
    : deviceRefreshSpinner_(deviceRefreshProgress_),
      audioEngine_(audioEngine),
      audio_(audioEngine != nullptr ? audioEngine->getAudioIO() : nullptr) {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // Driver first: it decides which interfaces the pickers below list.
    driverLabel_.setText(tr("audio_settings.label.driver"), juce::dontSendNotification);
    driverLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(driverLabel_);

    driverComboBox_.onChange = [this]() { onDriverSelected(); };
    addAndMakeVisible(driverComboBox_);

    // Input device selection dropdown
    inputDeviceLabel_.setText(tr("audio_settings.label.input_interface"),
                              juce::dontSendNotification);
    inputDeviceLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(inputDeviceLabel_);

    inputDeviceComboBox_.onChange = [this]() { onInputDeviceSelected(); };
    addAndMakeVisible(inputDeviceComboBox_);

    // Output device selection dropdown
    outputDeviceLabel_.setText(tr("audio_settings.label.output_interface"),
                               juce::dontSendNotification);
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
    deviceRefreshSpinner_.setTooltip(tr("audio_settings.tooltip.refreshing"));
    addAndMakeVisible(deviceRefreshSpinner_);
    deviceRefreshSpinner_.setVisible(false);

    deviceRefreshLabel_.setText(trEllipsis("audio_settings.status.refreshing"),
                                juce::dontSendNotification);
    deviceRefreshLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    deviceRefreshLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    deviceRefreshLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(deviceRefreshLabel_);
    deviceRefreshLabel_.setVisible(false);

    populateDeviceLists();

    // "Set as preferred devices" checkbox
    setAsPreferredCheckbox_.setButtonText(tr("audio_settings.toggle.set_preferred"));
    addAndMakeVisible(setAsPreferredCheckbox_);

    // Check if current devices match preferred devices in Config
    auto& config = magda::Config::getInstance();
    const auto chosenNow = audio_ != nullptr ? audio_->chosen() : AudioIOSettings{};
    bool inputMatches = chosenNow.inputInterface == config.getPreferredInputDevice();
    bool outputMatches = chosenNow.outputInterface == config.getPreferredOutputDevice();
    setAsPreferredCheckbox_.setToggleState(inputMatches && outputMatches,
                                           juce::dontSendNotification);

    // The stream the chosen interface runs at. Both apply through the choice, so what is
    // picked is what opens rather than something read back off the device afterwards.
    sampleRateLabel_.setText(tr("audio_settings.label.sample_rate"), juce::dontSendNotification);
    sampleRateLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(sampleRateLabel_);
    sampleRateComboBox_.onChange = [this]() { onSampleRateSelected(); };
    addAndMakeVisible(sampleRateComboBox_);

    bufferSizeLabel_.setText(tr("audio_settings.label.buffer_size"), juce::dontSendNotification);
    bufferSizeLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(bufferSizeLabel_);
    bufferSizeComboBox_.onChange = [this]() { onBufferSizeSelected(); };
    addAndMakeVisible(bufferSizeComboBox_);

    // MAGDA's own MIDI section, over the Config-backed choice (#2755).
    midiInputsLabel_.setText(tr("audio_settings.label.midi_inputs"), juce::dontSendNotification);
    midiInputsLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(midiInputsLabel_);

    if (audio_ != nullptr) {
        midiInputList_ = std::make_unique<MidiInputList>(*audio_);
        addAndMakeVisible(*midiInputList_);
    }

    midiOutputLabel_.setText(tr("audio_settings.label.midi_output"), juce::dontSendNotification);
    midiOutputLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(midiOutputLabel_);
    midiOutputComboBox_.onChange = [this]() {
        if (audio_ == nullptr)
            return;
        // By the identifier listed with the item: re-reading the device array here would
        // index a fresh list with an id from the old one, and pick the wrong output
        // whenever MIDI was plugged or unplugged since.
        const auto index = midiOutputComboBox_.getSelectedId() - 2;
        audio_->setDefaultMidiOutput(index >= 0 && index < static_cast<int>(midiOutputIds_.size())
                                         ? midiOutputIds_[static_cast<std::size_t>(index)]
                                         : juce::String());
    };
    addAndMakeVisible(midiOutputComboBox_);

    if (juce::BluetoothMidiDevicePairingDialogue::isAvailable()) {
        bluetoothMidiButton_.setButtonText(trEllipsis("audio_settings.button.bluetooth_midi"));
        bluetoothMidiButton_.onClick = [] { juce::BluetoothMidiDevicePairingDialogue::open(); };
        addAndMakeVisible(bluetoothMidiButton_);
    }

    // The choice announces itself, so nothing here watches a device manager.
    if (audio_ != nullptr)
        audio_->addListener(this);

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
    closeButton_.setButtonText(tr("audio_settings.button.close"));
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
    if (audio_ != nullptr)
        audio_->removeListener(this);
    setLookAndFeel(nullptr);
}

void AudioSettingsDialog::hardwareChannelsChanged() {
    // Deliberately not the MIDI controls. Rebuilding the input list reapplies every
    // Config-active port, and JUCE broadcasts a manager change after attempting the open
    // whether or not it succeeded -- so a busy port never reaches the state asked for, and
    // refreshing from here would retry it on its own notification forever. Hot-plug and
    // pairing come through MidiDeviceListConnection instead.

    // A channel toggle reopens the interface too, and its lists are already right.
    const auto chosen = audio_->chosen();
    if (chosen.backend == listed_.backend && chosen.inputInterface == listed_.inputInterface &&
        chosen.outputInterface == listed_.outputInterface) {
        showOpenInterface();
        populateStreamLists();
        hideDeviceRefreshIndicator();
        return;
    }

    showDeviceRefreshIndicator(true);
    refreshChosenInterface();
    hideDeviceRefreshIndicator();
}

void AudioSettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND));
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

    // Driver above the interfaces, which it decides the contents of
    auto driverArea = bounds.removeFromTop(28);
    driverLabel_.setBounds(driverArea.removeFromLeft(120));
    driverArea.removeFromLeft(10);  // spacing
    driverComboBox_.setBounds(driverArea);
    bounds.removeFromTop(5);  // spacing

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

    // Rate and block size share a row: both are short, and both describe the open stream
    // rather than which box it runs on.
    auto streamArea = bounds.removeFromTop(28);
    sampleRateLabel_.setBounds(streamArea.removeFromLeft(120));
    streamArea.removeFromLeft(10);  // spacing
    sampleRateComboBox_.setBounds(streamArea.removeFromLeft(110));
    streamArea.removeFromLeft(20);  // spacing
    bufferSizeLabel_.setBounds(streamArea.removeFromLeft(90));
    streamArea.removeFromLeft(10);  // spacing
    bufferSizeComboBox_.setBounds(streamArea.removeFromLeft(110));
    bounds.removeFromTop(5);  // spacing

    // Close button at bottom
    const int buttonHeight = 28;
    const int buttonWidth = 80;
    auto buttonArea = bounds.removeFromBottom(buttonHeight);
    bounds.removeFromBottom(10);  // spacing
    closeButton_.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, buttonHeight));

    // Split remaining space: MIDI on the left where the JUCE selector was, channels right
    auto midiArea = bounds.removeFromLeft(bounds.getWidth() / 2);
    bounds.removeFromLeft(10);  // spacing

    // Packed top-down: the list takes what its rows need and the rest of the column is
    // left empty, rather than stretching it and stranding the picker at the bottom.
    midiInputsLabel_.setBounds(midiArea.removeFromTop(22));

    if (midiInputList_ != nullptr) {
        // Capped so a machine with many inputs scrolls instead of pushing the rows below
        // it out of the dialog.
        const auto below = (bluetoothMidiButton_.isVisible() ? 26 + 8 : 0) + 28 + 8;
        const auto listHeight =
            juce::jmin(midiInputList_->preferredHeight(), midiArea.getHeight() - below);
        midiInputList_->setBounds(midiArea.removeFromTop(juce::jmax(22, listHeight)));
        midiArea.removeFromTop(8);  // spacing
    }

    if (bluetoothMidiButton_.isVisible()) {
        bluetoothMidiButton_.setBounds(midiArea.removeFromTop(26).removeFromLeft(140));
        midiArea.removeFromTop(8);  // spacing
    }

    auto midiOutputArea = midiArea.removeFromTop(28);
    midiOutputLabel_.setBounds(midiOutputArea.removeFromLeft(90));
    midiOutputArea.removeFromLeft(10);  // spacing
    midiOutputComboBox_.setBounds(midiOutputArea);

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

    // Off the chosen backend, which is what will be opened: listing the first backend's
    // interfaces once another was chosen failed with "No such device".
    const auto backend = juce::String(audio_->chosen().backend);
    const bool singleDeviceDriver = audio_->isSingleInterfaceBackend(backend);
    auto inputDevices = audio_->interfaceNames(backend, !singleDeviceDriver);
    auto outputDevices =
        singleDeviceDriver ? juce::StringArray() : audio_->interfaceNames(backend, false);

    driverComboBox_.clear(juce::dontSendNotification);
    const auto backends = audio_->backendNames();
    for (int i = 0; i < backends.size(); ++i)
        driverComboBox_.addItem(backends[i], i + 1);
    driverComboBox_.setSelectedId(backends.indexOf(backend) + 1, juce::dontSendNotification);

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
    const bool singleDeviceDriver =
        audio_ != nullptr && audio_->isSingleInterfaceBackend(audio_->chosen().backend);

    inputDeviceLabel_.setText(singleDeviceDriver ? tr("audio_settings.label.interface")
                                                 : tr("audio_settings.label.input_interface"),
                              juce::dontSendNotification);
    outputDeviceLabel_.setVisible(!singleDeviceDriver);
    outputDeviceComboBox_.setVisible(!singleDeviceDriver);
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
    if (audio_->isSingleInterfaceBackend(audio_->chosen().backend))
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
    const auto single = audio_->isSingleInterfaceBackend(audio_->chosen().backend);
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
            juce::AlertWindow::WarningIcon, tr("audio_settings.error.title"),
            tr("audio_settings.error.open").replace("{0}", interfaceName).replace("{1}", error));

    refreshChosenInterface();
    hideDeviceRefreshIndicator();
}

void AudioSettingsDialog::onDriverSelected() {
    const auto id = driverComboBox_.getSelectedId();
    if (id <= 0 || audio_ == nullptr)
        return;

    const auto backend = driverComboBox_.getItemText(id - 1);
    auto settings = audio_->chosen();
    if (juce::String(settings.backend) == backend)
        return;

    // Nothing is carried over: the interfaces and their channels belong to the backend
    // that named them, so the new one opens its own defaults (#2528).
    settings.backend = backend.toStdString();
    settings.outputInterface = audio_->defaultInterface(backend, false).toStdString();
    settings.inputInterface = audio_->defaultInterface(backend, true).toStdString();
    settings.inputChannels.clear();
    settings.outputChannels = {0, 1};

    showDeviceRefreshIndicator(true);
    if (const auto error = audio_->apply(settings); error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, tr("audio_settings.error.title"),
            tr("audio_settings.error.open").replace("{0}", backend).replace("{1}", error));
    refreshChosenInterface();
    hideDeviceRefreshIndicator();
}

void AudioSettingsDialog::onSampleRateSelected() {
    const auto id = sampleRateComboBox_.getSelectedId();
    if (id <= 0 || audio_ == nullptr)
        return;

    auto settings = audio_->chosen();
    const auto rate = sampleRateComboBox_.getItemText(id - 1).getDoubleValue();
    if (settings.sampleRate == rate)
        return;
    settings.sampleRate = rate;
    applyStreamChange(settings, tr("audio_settings.what.sample_rate"),
                      juce::String(rate, 0) + " Hz");
}

void AudioSettingsDialog::onBufferSizeSelected() {
    const auto id = bufferSizeComboBox_.getSelectedId();
    if (id <= 0 || audio_ == nullptr)
        return;

    auto settings = audio_->chosen();
    const auto size = bufferSizeComboBox_.getItemText(id - 1).getIntValue();
    if (settings.bufferSize == size)
        return;
    settings.bufferSize = size;
    applyStreamChange(settings, tr("audio_settings.what.buffer_size"),
                      tr("audio_settings.unit.samples").replace("{0}", juce::String(size)));
}

void AudioSettingsDialog::applyStreamChange(const AudioIOSettings& settings,
                                            const juce::String& what, const juce::String& asked) {
    deviceRefreshLabel_.setText(
        trEllipsis("audio_settings.status.changing").replace("{0}", what).replace("{1}", asked),
        juce::dontSendNotification);
    sampleRateComboBox_.setEnabled(false);
    bufferSizeComboBox_.setEnabled(false);
    showDeviceRefreshIndicator(false);

    audio_->applyAsync(settings, [safe = juce::Component::SafePointer(this), what,
                                  asked](const juce::String& error) {
        // A device can refuse a combination it advertises, and JUCE drops the open device
        // when it does -- silently, leaving no audio and no reason for it.
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("audio_settings.error.title"),
                                                   tr("audio_settings.error.set")
                                                       .replace("{0}", what)
                                                       .replace("{1}", asked)
                                                       .replace("{2}", error));
        if (safe == nullptr)
            return;

        // Either way: on success the lists follow the new stream, and on failure they show
        // what is actually open rather than what was asked for.
        safe->sampleRateComboBox_.setEnabled(true);
        safe->bufferSizeComboBox_.setEnabled(true);
        safe->refreshChosenInterface();
        safe->hideDeviceRefreshIndicator();
        safe->deviceRefreshLabel_.setText(trEllipsis("audio_settings.status.refreshing"),
                                          juce::dontSendNotification);
    });
}

void AudioSettingsDialog::populateStreamLists() {
    if (audio_ == nullptr)
        return;

    const auto status = audio_->status();

    sampleRateComboBox_.clear(juce::dontSendNotification);
    const auto rates = audio_->availableSampleRates();
    for (std::size_t i = 0; i < rates.size(); ++i) {
        sampleRateComboBox_.addItem(juce::String(rates[i], 0), static_cast<int>(i) + 1);
        if (std::abs(rates[i] - status.sampleRate) < 1.0)
            sampleRateComboBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }

    bufferSizeComboBox_.clear(juce::dontSendNotification);
    const auto sizes = audio_->availableBufferSizes();
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        bufferSizeComboBox_.addItem(juce::String(sizes[i]), static_cast<int>(i) + 1);
        if (sizes[i] == status.bufferSize)
            bufferSizeComboBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }
}

void AudioSettingsDialog::populateMidiOutputs() {
    if (audio_ == nullptr)
        return;

    midiOutputComboBox_.clear(juce::dontSendNotification);
    midiOutputComboBox_.addItem(tr("audio_settings.midi_output.none"), 1);

    const auto open = audio_->defaultMidiOutput();
    const auto devices = juce::MidiOutput::getAvailableDevices();
    midiOutputIds_.clear();
    for (int i = 0; i < devices.size(); ++i) {
        midiOutputComboBox_.addItem(devices[i].name, i + 2);
        midiOutputIds_.push_back(devices[i].identifier);
        if (devices[i].identifier == open)
            midiOutputComboBox_.setSelectedId(i + 2, juce::dontSendNotification);
    }
    if (midiOutputComboBox_.getSelectedId() == 0)
        midiOutputComboBox_.setSelectedId(1, juce::dontSendNotification);
}

void AudioSettingsDialog::refreshMidiControls() {
    if (midiInputList_ != nullptr)
        midiInputList_->refresh();
    populateMidiOutputs();
}

void AudioSettingsDialog::refreshChosenInterface() {
    listed_ = audio_->chosen();
    populateDeviceLists();
    populateStreamLists();
    populateMidiOutputs();
    inputChannelSelector_->refresh();
    outputChannelSelector_->refresh();
    showOpenInterface();
    resized();
}

void AudioSettingsDialog::showOpenInterface() {
    const auto status = audio_->status();
    if (status.interfaceName.isEmpty()) {
        deviceNameLabel_.setText(tr("audio_settings.status.no_interface"),
                                 juce::dontSendNotification);
        return;
    }

    const auto chosen = audio_->chosen();
    const auto ins = audio_->channelNames(chosen.backend, chosen.inputInterface, true).size();
    const auto outs = audio_->channelNames(chosen.backend, chosen.outputInterface, false).size();
    deviceNameLabel_.setText(tr("audio_settings.status.current")
                                 .replace("{0}", status.interfaceName)
                                 .replace("{1}", juce::String(ins))
                                 .replace("{2}", juce::String(outs)),
                             juce::dontSendNotification);
}

void AudioSettingsDialog::savePreferencesIfNeeded() {
    if (!setAsPreferredCheckbox_.getToggleState())
        return;

    const auto chosen = audio_->chosen();

    // Count enabled channels from the engine's wave-device state.
    int inputChannelCount = 0;
    int outputChannelCount = 0;
    if (auto* hardware = audioEngine_ != nullptr ? audioEngine_->getAudioIO() : nullptr) {
        inputChannelCount = hardware->inputs().open.getHighestBit() + 1;
        outputChannelCount = hardware->outputs().open.getHighestBit() + 1;
    } else {
        inputChannelCount = static_cast<int>(chosen.inputChannels.size());
        outputChannelCount = static_cast<int>(chosen.outputChannels.size());
    }

    // Save to Config
    auto& config = magda::Config::getInstance();
    juce::String preferredInputDevice(chosen.inputInterface);
    juce::String preferredOutputDevice(chosen.outputInterface);
    if (audio_->isSingleInterfaceBackend(chosen.backend)) {
        preferredInputDevice =
            preferredInputDevice.isNotEmpty() ? preferredInputDevice : preferredOutputDevice;
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

void AudioSettingsDialog::showDialog(juce::Component* parent, AudioEngine* audioEngine) {
    juce::ignoreUnused(parent);
    if (audioEngine == nullptr || audioEngine->getAudioIO() == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("audio_settings.error.not_initialized_title"),
                                               tr("audio_settings.error.not_initialized"));
        return;
    }

    auto* dialog = new AudioSettingsDialog(audioEngine);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("audio_settings.title");
    options.dialogBackgroundColour = ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

}  // namespace magda
