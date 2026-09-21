#include "ExternalInsertUI.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

#include "audio/io/AudioIOControl.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "themes/ActiveTheme.hpp"
#include "themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

using Endpoint = magda::InsertConfig::Endpoint;

magda::DeviceInfo* modelDevice(const magda::ChainNodePath& path) {
    return magda::TrackManager::getInstance().getDeviceInChainByPath(path);
}

/// Write @p edit into the insert at @p path and tell the engines, which rebuild it (#2279).
template <typename Edit> void editInsert(const magda::ChainNodePath& path, Edit&& edit) {
    auto& manager = magda::TrackManager::getInstance();
    if (auto* device = manager.getDeviceInChainByPath(path)) {
        edit(device->insert);
        manager.notifyDevicePropertyChanged(path);
    }
}

// The picker id whose mapped name matches the device's current port, else 0 ("None").
int idForName(const std::map<int, juce::String>& names, const juce::String& current) {
    for (const auto& [id, name] : names)
        if (name == current)
            return id;
    return 0;
}

/// The hardware channels an engine has open, by the names saved routes use, in channel order.
juce::StringArray routeNames(bool inputs) {
    juce::StringArray names;
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* io = engine != nullptr ? engine->getAudioIO() : nullptr;
    if (io == nullptr)
        return names;

    for (const auto& [channel, name] : inputs ? io->inputs().routeNames : io->outputs().routeNames)
        names.addIfNotAlreadyThere(name);
    return names;
}

juce::StringArray midiOutputNames() {
    juce::StringArray names;
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        names.addIfNotAlreadyThere(device.name);
    return names;
}

std::vector<magda::RoutingSelector::RoutingOption> buildOptions(
    const juce::StringArray& devices, std::map<int, juce::String>& names) {
    std::vector<magda::RoutingSelector::RoutingOption> options;
    options.push_back({0, "None", false});
    names.clear();

    if (!devices.isEmpty())
        options.push_back({-1, "", true});

    for (int i = 0; i < devices.size(); ++i) {
        const int id = i + 1;
        options.push_back({id, devices[i], false});
        names[id] = devices[i];
    }
    return options;
}

// Feedback-port guard (#1623): two inserts driving the same hardware send, or
// pulling the same return, silently double signals / cross-feed. Returns a
// warning string when another enabled external insert shares one of this
// insert's ports, empty otherwise.
juce::String findPortConflict(const magda::ChainNodePath& myPath, const magda::InsertConfig& mine) {
    const auto& mySend = mine.sendDevice;
    const auto& myReturn = mine.returnDevice;
    if (mySend.isEmpty() && myReturn.isEmpty())
        return {};
    if (mySend.isNotEmpty() && mySend == myReturn)
        return "Return uses the same port as the send";

    for (const auto& track : magda::TrackManager::getInstance().getTracks()) {
        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;
            const auto& device = magda::getDevice(element);
            if (device.bypassed ||
                !magda::daw::audio::internalPluginHasTag(device.pluginId, "external-insert"))
                continue;
            if (magda::ChainNodePath::topLevelDevice(track.id, device.id) == myPath)
                continue;
            if (mySend.isNotEmpty() && device.insert.sendDevice == mySend)
                return "Send port also used on " + track.name;
            if (myReturn.isNotEmpty() && device.insert.returnDevice == myReturn)
                return "Return port also used on " + track.name;
        }
    }
    return {};
}

}  // namespace

ExternalInsertUI::ExternalInsertUI(bool isInstrument) : isInstrument_(isInstrument) {
    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.0f));
        label.setColour(juce::Label::textColourId,
                        ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
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
                            ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND));
    latencyValue_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    latencyValue_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyValue_);
    latencyValue_.onTextChange = [this] {
        const auto ms = latencyValue_.getText().getDoubleValue();
        editInsert(devicePath_, [ms](magda::InsertConfig& insert) { insert.manualAdjustMs = ms; });
    };

    warningLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    warningLabel_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    warningLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(warningLabel_);
}

void ExternalInsertUI::setDevicePath(const magda::ChainNodePath& path) {
    devicePath_ = path;
    rebuildFromModel();
}

void ExternalInsertUI::rebuildFromModel() {
    const auto* device = modelDevice(devicePath_);
    if (device == nullptr)
        return;

    const auto sendType = isInstrument_ ? Endpoint::MIDI : Endpoint::Audio;

    sendSelector_->setOptions(
        buildOptions(isInstrument_ ? midiOutputNames() : routeNames(false), sendNames_));
    sendSelector_->setSelectedId(idForName(sendNames_, device->insert.sendDevice));
    sendSelector_->onSelectionChanged = [this, sendType](int id) {
        const auto name = id <= 0 ? juce::String() : sendNames_[id];
        editInsert(devicePath_, [&](magda::InsertConfig& insert) {
            insert.sendDevice = name;
            insert.sendType = name.isEmpty() ? Endpoint::None : sendType;
        });
        refreshConflictWarning();
    };

    returnSelector_->setOptions(buildOptions(routeNames(true), returnNames_));
    returnSelector_->setSelectedId(idForName(returnNames_, device->insert.returnDevice));
    returnSelector_->onSelectionChanged = [this](int id) {
        const auto name = id <= 0 ? juce::String() : returnNames_[id];
        editInsert(devicePath_, [&](magda::InsertConfig& insert) {
            insert.returnDevice = name;
            insert.returnType = name.isEmpty() ? Endpoint::None : Endpoint::Audio;
        });
        refreshConflictWarning();
    };

    latencyValue_.setText(juce::String(device->insert.manualAdjustMs, 1),
                          juce::dontSendNotification);

    refreshConflictWarning();
}

void ExternalInsertUI::refreshConflictWarning() {
    juce::String conflict;
    if (const auto* device = modelDevice(devicePath_))
        conflict = findPortConflict(devicePath_, device->insert);
    warningLabel_.setText(conflict, juce::dontSendNotification);
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

    warningLabel_.setBounds(bounds.removeFromTop(rowHeight));
}

}  // namespace magda::daw::ui
