#include "ExternalInsertUI.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "audio/AudioBridge.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "themes/DarkTheme.hpp"
#include "themes/FontManager.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

namespace {

// Resolve the live te::InsertPlugin for a device path, or nullptr if the slot is
// not yet bound to a running plugin.
te::InsertPlugin* liveInsert(const magda::ChainNodePath& path) {
    if (auto* engine = magda::TrackManager::getInstance().getAudioEngine())
        if (auto* bridge = engine->getAudioBridge())
            if (auto plugin = bridge->getPlugin(path))
                return dynamic_cast<te::InsertPlugin*>(plugin.get());
    return nullptr;
}

// The picker id whose mapped name matches the plugin's current device, else 0
// ("None").
int idForName(const std::map<int, juce::String>& names, const juce::String& current) {
    for (const auto& [id, name] : names)
        if (name == current)
            return id;
    return 0;
}

// Build picker options from te::InsertPlugin::getPossibleDeviceNames, keeping
// only audio (or MIDI) endpoints. The returned names round-trip into the
// plugin's input/output device CachedValues verbatim.
std::vector<magda::RoutingSelector::RoutingOption> buildOptions(
    te::Engine& engine, bool forInput, bool wantMidi, std::map<int, juce::String>& names) {
    juce::StringArray devices, aliases;
    juce::BigInteger hasAudio, hasMidi;
    te::InsertPlugin::getPossibleDeviceNames(engine, devices, aliases, hasAudio, hasMidi, forInput);

    std::vector<magda::RoutingSelector::RoutingOption> options;
    options.push_back({0, "None", false});
    names.clear();

    bool addedSeparator = false;
    for (int i = 0; i < devices.size(); ++i) {
        const bool ok = wantMidi ? hasMidi[i] : hasAudio[i];
        if (!ok)
            continue;
        if (!addedSeparator) {
            options.push_back({-1, "", true});
            addedSeparator = true;
        }
        const int id = i + 1;
        options.push_back({id, devices[i], false});
        names[id] = devices[i];
    }
    return options;
}

}  // namespace

ExternalInsertUI::ExternalInsertUI(bool isInstrument) : isInstrument_(isInstrument) {
    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.0f));
        label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    };

    setupLabel(sendLabel_, isInstrument_ ? "MIDI to" : "Send to");
    setupLabel(returnLabel_, "Return from");
    setupLabel(latencyLabel_, "Latency (ms)");

    sendSelector_ = std::make_unique<magda::RoutingSelector>(
        isInstrument_ ? magda::RoutingSelector::Type::MidiOut
                      : magda::RoutingSelector::Type::AudioOut);
    returnSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioIn);
    addAndMakeVisible(*sendSelector_);
    addAndMakeVisible(*returnSelector_);

    latencyValue_.setEditable(true);
    latencyValue_.setText("0.0", juce::dontSendNotification);
    latencyValue_.setColour(juce::Label::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    latencyValue_.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    latencyValue_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyValue_);
    latencyValue_.onTextChange = [this] {
        if (auto* insert = liveInsert(devicePath_))
            insert->manualAdjustMs = latencyValue_.getText().getDoubleValue();
    };
}

void ExternalInsertUI::setDevicePath(const magda::ChainNodePath& path) {
    devicePath_ = path;
    rebuildFromPlugin();
}

void ExternalInsertUI::rebuildFromPlugin() {
    auto* insert = liveInsert(devicePath_);
    if (insert == nullptr)
        return;

    auto& engine = insert->edit.engine;

    auto sendOptions =
        buildOptions(engine, /*forInput*/ false, /*wantMidi*/ isInstrument_, sendNames_);
    sendSelector_->setOptions(sendOptions);
    sendSelector_->setSelectedId(idForName(sendNames_, insert->outputDevice.get()));
    sendSelector_->onSelectionChanged = [this](int id) {
        if (auto* ins = liveInsert(devicePath_)) {
            ins->outputDevice = (id <= 0) ? juce::String() : sendNames_[id];
            ins->updateDeviceTypes();
        }
    };

    auto returnOptions = buildOptions(engine, /*forInput*/ true, /*wantMidi*/ false, returnNames_);
    returnSelector_->setOptions(returnOptions);
    returnSelector_->setSelectedId(idForName(returnNames_, insert->inputDevice.get()));
    returnSelector_->onSelectionChanged = [this](int id) {
        if (auto* ins = liveInsert(devicePath_)) {
            ins->inputDevice = (id <= 0) ? juce::String() : returnNames_[id];
            ins->updateDeviceTypes();
        }
    };

    latencyValue_.setText(juce::String(insert->manualAdjustMs.get(), 1),
                          juce::dontSendNotification);
}

void ExternalInsertUI::resized() {
    auto bounds = getLocalBounds().reduced(4);
    const int rowHeight = 26;
    const int gap = 4;
    const int labelWidth = 90;

    auto layoutRow = [&](juce::Label& label, juce::Component& control) {
        auto row = bounds.removeFromTop(rowHeight);
        label.setBounds(row.removeFromLeft(labelWidth));
        control.setBounds(row);
        bounds.removeFromTop(gap);
    };

    layoutRow(sendLabel_, *sendSelector_);
    layoutRow(returnLabel_, *returnSelector_);
    layoutRow(latencyLabel_, latencyValue_);
}

}  // namespace magda::daw::ui
