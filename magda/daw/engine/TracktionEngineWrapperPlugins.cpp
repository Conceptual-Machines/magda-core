#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <utility>

#include "../audio/AudioBridge.hpp"
#include "../audio/plugin_manager/ExternalPluginState.hpp"
#include "../audio/plugin_manager/ExternalPluginStateUtil.hpp"
#include "../audio/plugins/InternalPluginRegistry.hpp"
#include "../audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "../audio/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "../audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "TracktionEngineWrapper.hpp"
#include "core/Config.hpp"

namespace magda {
std::string TracktionEngineWrapper::addEffect(const std::string& track_id,
                                              const std::string& effect_name) {
    // TODO: Implement effect addition
    auto effectId = generateEffectId();
    DBG("Added effect (stub): " << effect_name << " to track " << track_id);
    return effectId;
}

void TracktionEngineWrapper::removeEffect(const std::string& effect_id) {
    // TODO: Implement effect removal
    DBG("Removed effect (stub): " << effect_id);
}

void TracktionEngineWrapper::setEffectParameter(const std::string& effect_id,
                                                const std::string& parameter_name, double value) {
    // TODO: Implement effect parameter setting
    DBG("Set effect parameter (stub): " << effect_id << "." << parameter_name << " = " << value);
}

double TracktionEngineWrapper::getEffectParameter(const std::string& effect_id,
                                                  const std::string& parameter_name) const {
    // TODO: Implement effect parameter retrieval
    return 0.0;
}

void TracktionEngineWrapper::setEffectEnabled(const std::string& effect_id, bool enabled) {
    // TODO: Implement effect enable/disable
    DBG("Set effect enabled (stub): " << effect_id << " = " << (int)enabled);
}

bool TracktionEngineWrapper::isEffectEnabled(const std::string& effect_id) const {
    // TODO: Implement effect enabled check
    return true;
}

std::vector<std::string> TracktionEngineWrapper::getAvailableEffects() const {
    // TODO: Implement available effects retrieval
    return {"Reverb", "Delay", "EQ", "Compressor"};
}

std::vector<std::string> TracktionEngineWrapper::getTrackEffects(
    const std::string& track_id) const {
    // TODO: Implement track effects retrieval
    return {};
}

juce::KnownPluginList& TracktionEngineWrapper::getKnownPluginList() {
    return engine_->getPluginManager().knownPluginList;
}

const juce::KnownPluginList& TracktionEngineWrapper::getKnownPluginList() const {
    return engine_->getPluginManager().knownPluginList;
}

juce::AudioPluginFormatManager& TracktionEngineWrapper::getPluginFormatManager() {
    return engine_->getPluginManager().pluginFormatManager;
}

std::vector<ScannedPluginParameter> TracktionEngineWrapper::scanInternalParametersInEdit(
    const juce::String& pluginId) {
    std::vector<ScannedPluginParameter> result;
    if (!engine_ || !currentEdit_)
        return result;

    constexpr std::array<float, 5> samplePoints{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    namespace te = tracktion::engine;
    te::Plugin::Ptr plugin;
    if (const auto* spec = daw::audio::findInternalPluginSpec(pluginId)) {
        plugin = daw::audio::tracktion_adapter::createInternalPlugin(*spec, *currentEdit_);
    } else {
        juce::ValueTree state(te::IDs::PLUGIN);
        state.setProperty(te::IDs::type, pluginId, nullptr);
        plugin = currentEdit_->getPluginCache().createNewPlugin(state);
    }
    if (plugin == nullptr)
        return result;

    const auto parameters = plugin->getAutomatableParameters();
    for (int i = 0; i < parameters.size(); ++i) {
        auto* parameter = parameters[i];
        if (parameter == nullptr || parameter->getParameterName().isEmpty())
            continue;

        const auto range = parameter->getValueRange();
        const int numStates = parameter->getNumberOfStates();
        ScannedPluginParameter info;
        info.name = parameter->getParameterName();
        info.stableId = parameter->paramID;
        info.defaultValue = parameter->getDefaultValue().value_or(range.getStart());
        info.unit = "%";
        info.rangeMin = range.getStart();
        info.rangeMax = range.getEnd();
        info.rangeCenter = (info.rangeMin + info.rangeMax) * 0.5f;
        info.scale = numStates == 2 ? ParameterScale::Boolean
                                    : (numStates > 0 && numStates <= 12 ? ParameterScale::Discrete
                                                                        : ParameterScale::Linear);
        // The position in this list, not the loop counter: a parameter skipped
        // above leaves the two apart, and a detection result is applied by position.
        info.scanInput.paramIndex = static_cast<int>(result.size());
        info.scanInput.name = info.name;
        info.scanInput.label = parameter->getLabel();
        info.scanInput.rangeMin = info.rangeMin;
        info.scanInput.rangeMax = info.rangeMax;
        info.scanInput.stateCount = (numStates > 1 && numStates <= 1000) ? numStates : 0;
        for (const auto sample : samplePoints) {
            const float raw = info.rangeMin + (info.rangeMax - info.rangeMin) * sample;
            info.scanInput.displayTexts.push_back(parameter->valueToString(raw));
        }
        if (info.scale == ParameterScale::Discrete || info.scale == ParameterScale::Boolean)
            info.valueTable = info.scanInput.displayTexts;
        result.push_back(std::move(info));
    }
    return result;
}

bool TracktionEngineWrapper::upsertGrooveTemplate(const GrooveTemplateData& data) {
    if (!engine_ || data.name.isEmpty() || data.latenessProportions.empty())
        return false;

    tracktion::GrooveTemplate groove;
    groove.setName(data.name);
    groove.setNumberOfNotes(static_cast<int>(data.latenessProportions.size()));
    groove.setNotesPerBeat(data.notesPerBeat);
    groove.setParameterized(data.parameterized);
    for (int i = 0; i < static_cast<int>(data.latenessProportions.size()); ++i)
        groove.setLatenessProportion(i, data.latenessProportions[static_cast<size_t>(i)], 1.0f);

    auto& manager = engine_->getGrooveTemplateManager();
    manager.useParameterizedGrooves(true);
    int existingIndex = -1;
    for (int i = 0; i < manager.getNumTemplates(); ++i) {
        if (manager.getTemplateName(i) == data.name) {
            existingIndex = i;
            break;
        }
    }
    manager.updateTemplate(existingIndex, groove);
    return true;
}

juce::StringArray TracktionEngineWrapper::getGrooveTemplateNames() const {
    return engine_ != nullptr ? engine_->getGrooveTemplateManager().getTemplateNames()
                              : juce::StringArray{};
}

std::vector<GrooveTemplateData> TracktionEngineWrapper::readGrooveTemplates() {
    std::vector<GrooveTemplateData> grooves;
    if (engine_ == nullptr)
        return grooves;

    // The manager's getters read its active list, which leaves out every parameterized
    // groove until this is on -- including the swing presets it ships (#2757). upsert
    // turns it on too, so the library would otherwise gain them only after a write.
    auto& manager = engine_->getGrooveTemplateManager();
    manager.useParameterizedGrooves(true);

    for (int i = 0; i < manager.getNumTemplates(); ++i) {
        const auto* groove = manager.getTemplate(i);
        if (groove == nullptr)
            continue;

        GrooveTemplateData data;
        data.name = groove->getName();
        data.notesPerBeat = groove->getNotesPerBeat();
        data.parameterized = groove->isParameterized();
        // Full strength: the stored proportion is what the library holds, and a clip's
        // own strength is folded in when an engine compiles the groove.
        for (int note = 0; note < groove->getNumberOfNotes(); ++note)
            data.latenessProportions.push_back(groove->getLatenessProportion(note, 1.0f));
        grooves.push_back(std::move(data));
    }
    return grooves;
}

void TracktionEngineWrapper::captureAllPluginStates() {
    if (audioBridge_ != nullptr)
        audioBridge_->captureAllPluginStates();
}

void TracktionEngineWrapper::capturePluginStateAt(const ChainNodePath& devicePath) {
    if (audioBridge_ != nullptr)
        audioBridge_->getPluginManager().capturePluginState(devicePath);
}

void TracktionEngineWrapper::applyPluginStateAt(const ChainNodePath& devicePath) {
    if (audioBridge_ == nullptr)
        return;

    auto* live = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (live == nullptr)
        return;

    auto plugin = audioBridge_->getPlugin(devicePath);
    if (plugin == nullptr)
        return;

    if (dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin.get()) == nullptr) {
        namespace ta = daw::audio::tracktion_adapter;
        if (const auto savedState = ta::devicePluginTreeFromState(live->pluginState);
            savedState.isValid())
            plugin->restorePluginStateFromValueTree(savedState);

        return;
    }

    // A preset with no chunk must not repopulate: that would discard the
    // parameter values it carried.
    if (!live->hasPluginState())
        return;

    applyExternalPluginChunk(plugin.get(), live->pluginState);

    if (auto* processor = audioBridge_->getDeviceProcessor(devicePath))
        processor->populateParameters(*live, DeviceProcessor::ValueSource::Engine);
}

std::optional<PluginPrograms> TracktionEngineWrapper::getPluginPrograms(const ChainNodePath& path) {
    if (!audioBridge_)
        return std::nullopt;
    PluginPrograms programs;
    const int count = audioBridge_->getPluginNumPrograms(path);
    programs.current = count > 0 ? audioBridge_->getPluginCurrentProgram(path) : -1;
    for (int i = 0; i < count; ++i)
        programs.names.add(audioBridge_->getPluginProgramName(path, i));
    return programs;
}
bool TracktionEngineWrapper::setPluginCurrentProgram(const ChainNodePath& path, int index) {
    return audioBridge_ && audioBridge_->setPluginCurrentProgram(path, index);
}
bool TracktionEngineWrapper::loadPluginPresetFile(const ChainNodePath& path,
                                                  const juce::File& file) {
    return audioBridge_ && audioBridge_->loadPluginPresetFile(path, file);
}
bool TracktionEngineWrapper::savePluginPresetFile(const ChainNodePath& path,
                                                  const juce::File& file) {
    return audioBridge_ && audioBridge_->savePluginPresetFile(path, file);
}

bool TracktionEngineWrapper::showDeviceEditor(const ChainNodePath& devicePath) {
    if (audioBridge_ == nullptr)
        return false;

    audioBridge_->showPluginWindow(devicePath);
    return audioBridge_->isPluginWindowOpen(devicePath);
}

bool TracktionEngineWrapper::hideDeviceEditor(const ChainNodePath& devicePath) {
    if (audioBridge_ == nullptr)
        return false;

    // The bridge has no hide: a toggle closes an open window, and a closed one
    // needs nothing.
    if (audioBridge_->isPluginWindowOpen(devicePath))
        audioBridge_->togglePluginWindow(devicePath);

    return audioBridge_->isPluginWindowOpen(devicePath);
}

bool TracktionEngineWrapper::toggleDeviceEditor(const ChainNodePath& devicePath) {
    return audioBridge_ != nullptr && audioBridge_->togglePluginWindow(devicePath);
}

bool TracktionEngineWrapper::isDeviceEditorOpen(const ChainNodePath& devicePath) const {
    return audioBridge_ != nullptr && audioBridge_->isPluginWindowOpen(devicePath);
}

juce::String TracktionEngineWrapper::formatDeviceParameter(const ChainNodePath& devicePath,
                                                           int paramIndex, float normalised) const {
    if (audioBridge_ == nullptr)
        return {};

    auto* processor = audioBridge_->getDeviceProcessor(devicePath);
    return processor != nullptr ? processor->formatParameterValue(paramIndex, normalised)
                                : juce::String{};
}

std::shared_ptr<daw::audio::MagdaDevice> TracktionEngineWrapper::renderedDevice(
    const ChainNodePath& devicePath) const {
    if (audioBridge_ == nullptr)
        return {};

    return daw::audio::tracktion_adapter::deviceHandleFromPlugin(
        audioBridge_->getPlugin(devicePath));
}

}  // namespace magda
