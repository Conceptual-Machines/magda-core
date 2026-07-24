#include "ExternalInsertUI.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "audio/AudioBridge.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
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

// After a send/return device change: the insert's send/return is wired straight
// into TE's playback graph at build time (applyToBuffer is a dead stub), so the
// new routing only takes effect once the graph is rebuilt. Then broadcast a
// device-property change so the track-level read-only routing mirror
// (TrackInspector / TrackHeadersPanel) refreshes in real time.
void rebuildPlaybackGraph(te::InsertPlugin& insert, const magda::ChainNodePath& path) {
    if (auto* ctx = insert.edit.getCurrentPlaybackContext();
        ctx != nullptr && ctx->isPlaybackGraphAllocated())
        ctx->reallocate();
    magda::TrackManager::getInstance().notifyDevicePropertyChanged(path);
}

// The picker id whose mapped name matches the plugin's current device, else 0
// ("None").
int idForName(const std::map<int, juce::String>& names, const juce::String& current) {
    for (const auto& [id, name] : names)
        if (name == current)
            return id;
    return 0;
}

// Build picker options from the device manager, keeping only audio (or MIDI)
// endpoints. Unlike te::InsertPlugin::getPossibleDeviceNames this lists
// DISABLED ports too — picking one auto-enables it (#1623 Phase 3,
// ExternalInsertDeviceEnablement). The names round-trip into the plugin's
// input/output device CachedValues verbatim.
std::vector<magda::RoutingSelector::RoutingOption> buildOptions(
    te::Engine& engine, bool forInput, bool wantMidi, std::map<int, juce::String>& names) {
    juce::StringArray devices;
    auto& dm = engine.getDeviceManager();
    if (forInput) {
        for (int i = 0; i < dm.getNumInputDevices(); ++i) {
            auto* in = dm.getInputDevice(i);
            if (in == nullptr)
                continue;
            const bool isMidi = dynamic_cast<te::MidiInputDevice*>(in) != nullptr;
            if (isMidi != wantMidi)
                continue;
            devices.add(in->getName());
        }
    } else {
        for (int i = 0; i < dm.getNumOutputDevices(); ++i) {
            auto* out = dm.getOutputDeviceAt(i);
            if (out == nullptr)
                continue;
            auto* midiOut = dynamic_cast<te::MidiOutputDevice*>(out);
            if ((midiOut != nullptr) != wantMidi)
                continue;
            // Same exclusion TE's own enumeration applies.
            if (midiOut != nullptr && midiOut->isConnectedToExternalController())
                continue;
            devices.add(out->getName());
        }
    }

    std::vector<magda::RoutingSelector::RoutingOption> options;
    options.push_back({0, "None", false});
    names.clear();

    bool addedSeparator = false;
    for (int i = 0; i < devices.size(); ++i) {
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

// Feedback-port guard (#1623): two inserts driving the same hardware send, or
// pulling the same return, silently double signals / cross-feed. Returns a
// warning string when another enabled external insert shares one of this
// insert's ports, empty otherwise.
juce::String findPortConflict(const magda::ChainNodePath& myPath, te::InsertPlugin& mine) {
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return {};

    const auto mySend = mine.outputDevice.get();
    const auto myReturn = mine.inputDevice.get();
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
            auto path = magda::ChainNodePath::topLevelDevice(track.id, device.id);
            if (path == myPath)
                continue;
            auto plugin = bridge->getPlugin(path);
            auto* other = dynamic_cast<te::InsertPlugin*>(plugin.get());
            if (other == nullptr)
                continue;
            if (mySend.isNotEmpty() && other->outputDevice.get() == mySend)
                return "Send port also used on " + track.name;
            if (myReturn.isNotEmpty() && other->inputDevice.get() == myReturn)
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

    warningLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    warningLabel_.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    warningLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(warningLabel_);
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
            rebuildPlaybackGraph(*ins, devicePath_);
            refreshConflictWarning();
        }
    };

    auto returnOptions = buildOptions(engine, /*forInput*/ true, /*wantMidi*/ false, returnNames_);
    returnSelector_->setOptions(returnOptions);
    returnSelector_->setSelectedId(idForName(returnNames_, insert->inputDevice.get()));
    returnSelector_->onSelectionChanged = [this](int id) {
        if (auto* ins = liveInsert(devicePath_)) {
            ins->inputDevice = (id <= 0) ? juce::String() : returnNames_[id];
            ins->updateDeviceTypes();
            rebuildPlaybackGraph(*ins, devicePath_);
            refreshConflictWarning();
        }
    };

    latencyValue_.setText(juce::String(insert->manualAdjustMs.get(), 1),
                          juce::dontSendNotification);

    refreshConflictWarning();
}

void ExternalInsertUI::refreshConflictWarning() {
    juce::String conflict;
    if (auto* insert = liveInsert(devicePath_))
        conflict = findPortConflict(devicePath_, *insert);
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

    juce::ignoreUnused(gap);
    warningLabel_.setBounds(bounds.removeFromTop(rowHeight));
}

}  // namespace magda::daw::ui
