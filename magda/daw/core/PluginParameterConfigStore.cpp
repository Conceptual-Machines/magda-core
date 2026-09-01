#include "PluginParameterConfigStore.hpp"

#include <algorithm>

#include "AppPaths.hpp"
#include "DeviceInfo.hpp"
#include "RackInfo.hpp"
#include "TrackManager.hpp"

namespace magda::PluginParameterConfigStore {
namespace {

/// The id a live device's config is filed under. Older devices carry only a
/// pluginId, so it is the fallback rather than an error.
juce::String configIdFor(const DeviceInfo& device) {
    return device.uniqueId.isNotEmpty() ? device.uniqueId : device.pluginId;
}

bool applyConfigToMatchingDevice(const juce::String& uniqueId, DeviceInfo& device) {
    if (configIdFor(device) != uniqueId)
        return false;
    return applyToDevice(uniqueId, device);
}

bool refreshElementParameterConfig(const juce::String& uniqueId,
                                   std::vector<ChainElement>& elements) {
    bool changed = false;
    for (auto& element : elements) {
        if (isDevice(element)) {
            changed = applyConfigToMatchingDevice(uniqueId, getDevice(element)) || changed;
        } else if (isRack(element)) {
            for (auto& chain : getRack(element).chains)
                changed = refreshElementParameterConfig(uniqueId, chain.elements) || changed;
        }
    }
    return changed;
}

bool refreshFlatParameterConfig(const juce::String& uniqueId,
                                std::vector<PostFxChainElement>& elements) {
    bool changed = false;
    for (auto& element : elements)
        changed = applyConfigToMatchingDevice(uniqueId, element.device) || changed;
    return changed;
}

}  // namespace

juce::String scaleToString(ParameterScale scale) {
    switch (scale) {
        case ParameterScale::Linear:
            return "linear";
        case ParameterScale::Logarithmic:
            return "logarithmic";
        case ParameterScale::Exponential:
            return "exponential";
        case ParameterScale::Discrete:
            return "discrete";
        case ParameterScale::Boolean:
            return "boolean";
        case ParameterScale::FaderDB:
            return "fader_db";
    }
    return "linear";
}

ParameterScale scaleFromString(const juce::String& name) {
    if (name == "logarithmic")
        return ParameterScale::Logarithmic;
    if (name == "exponential")
        return ParameterScale::Exponential;
    if (name == "discrete")
        return ParameterScale::Discrete;
    if (name == "boolean")
        return ParameterScale::Boolean;
    if (name == "fader_db")
        return ParameterScale::FaderDB;
    return ParameterScale::Linear;
}

juce::File configFileFor(const juce::String& uniqueId) {
    return paths::pluginConfigsDir().getChildFile(uniqueId.replaceCharacters(":/\\,; ", "______") +
                                                  ".xml");
}

std::optional<PluginParameterConfig> load(const juce::String& uniqueId) {
    if (uniqueId.isEmpty())
        return std::nullopt;

    const auto file = configFileFor(uniqueId);
    if (!file.existsAsFile())
        return std::nullopt;

    const auto xml = juce::parseXML(file);
    if (xml == nullptr)
        return std::nullopt;

    PluginParameterConfig config;
    config.pluginId = xml->getStringAttribute("pluginId", uniqueId);
    if (auto* promptElem = xml->getChildByName("AISoundDesignerPrompt"))
        config.aiPrompt = promptElem->getAllSubText().trim();

    if (auto* paramsElem = xml->getChildByName("Parameters")) {
        for (auto* paramElem : paramsElem->getChildIterator()) {
            PluginParameterConfigEntry entry;
            entry.index = paramElem->getIntAttribute("index", -1);
            if (entry.index < 0)
                continue;
            entry.name = paramElem->getStringAttribute("name");
            entry.visible = paramElem->getBoolAttribute("visible", false);
            entry.miniMixer = paramElem->getBoolAttribute("mini", false);
            entry.aiAgent = paramElem->getBoolAttribute("ai", false);
            if (paramElem->hasAttribute("unit"))
                entry.unit = paramElem->getStringAttribute("unit");
            if (paramElem->hasAttribute("scale"))
                entry.scale = scaleFromString(paramElem->getStringAttribute("scale"));
            if (paramElem->hasAttribute("min"))
                entry.rangeMin = static_cast<float>(paramElem->getDoubleAttribute("min"));
            if (paramElem->hasAttribute("max"))
                entry.rangeMax = static_cast<float>(paramElem->getDoubleAttribute("max"));
            if (paramElem->hasAttribute("center"))
                entry.rangeCenter = static_cast<float>(paramElem->getDoubleAttribute("center"));
            if (auto* choicesElem = paramElem->getChildByName("Choices")) {
                std::vector<juce::String> choices;
                for (auto* choice : choicesElem->getChildIterator())
                    choices.push_back(choice->getStringAttribute("label"));
                entry.choices = std::move(choices);
            }
            if (paramElem->hasAttribute("valueTable")) {
                std::vector<juce::String> table;
                for (const auto& token : juce::StringArray::fromTokens(
                         paramElem->getStringAttribute("valueTable"), "|", ""))
                    table.push_back(token);
                entry.valueTable = std::move(table);
            }
            config.entries.push_back(std::move(entry));
        }
    } else if (auto* visibleParams = xml->getChildByName("VisibleParameters")) {
        // Legacy format: a bare list of visible indices, nothing else.
        for (auto* paramElem : visibleParams->getChildIterator()) {
            const int index = paramElem->getIntAttribute("index", -1);
            if (index < 0)
                continue;
            PluginParameterConfigEntry entry;
            entry.index = index;
            entry.visible = true;
            config.entries.push_back(std::move(entry));
        }
    }
    return config;
}

bool save(const juce::String& uniqueId, const PluginParameterConfig& config) {
    if (uniqueId.isEmpty())
        return false;

    auto configDir = paths::pluginConfigsDir();
    if (!configDir.exists())
        configDir.createDirectory();

    juce::XmlElement root("ParameterConfig");
    root.setAttribute("pluginId", config.pluginId.isNotEmpty() ? config.pluginId : uniqueId);

    auto* paramsElem = root.createNewChildElement("Parameters");
    for (const auto& entry : config.entries) {
        auto* paramElem = paramsElem->createNewChildElement("Param");
        paramElem->setAttribute("index", entry.index);
        paramElem->setAttribute("name", entry.name);
        paramElem->setAttribute("visible", entry.visible);
        paramElem->setAttribute("mini", entry.miniMixer);
        paramElem->setAttribute("ai", entry.aiAgent);
        if (entry.unit)
            paramElem->setAttribute("unit", *entry.unit);
        if (entry.scale)
            paramElem->setAttribute("scale", scaleToString(*entry.scale));
        if (entry.rangeMin)
            paramElem->setAttribute("min", static_cast<double>(*entry.rangeMin));
        if (entry.rangeMax)
            paramElem->setAttribute("max", static_cast<double>(*entry.rangeMax));
        if (entry.rangeCenter)
            paramElem->setAttribute("center", static_cast<double>(*entry.rangeCenter));
        if (entry.choices && !entry.choices->empty()) {
            auto* choicesElem = paramElem->createNewChildElement("Choices");
            for (const auto& choice : *entry.choices)
                choicesElem->createNewChildElement("Choice")->setAttribute("label", choice);
        }
        if (entry.valueTable && !entry.valueTable->empty()) {
            juce::String tableStr;
            for (size_t j = 0; j < entry.valueTable->size(); ++j) {
                if (j > 0)
                    tableStr += "|";
                tableStr += (*entry.valueTable)[j];
            }
            paramElem->setAttribute("valueTable", tableStr);
        }
    }

    if (config.aiPrompt.isNotEmpty())
        root.createNewChildElement("AISoundDesignerPrompt")->addTextElement(config.aiPrompt);

    return root.writeTo(configFileFor(uniqueId));
}

PluginParameterConfig fromDevice(const DeviceInfo& device) {
    PluginParameterConfig config;
    config.pluginId = configIdFor(device);
    config.aiPrompt = device.aiSoundDesignerPrompt;
    config.entries.reserve(device.parameters.size());
    for (size_t i = 0; i < device.parameters.size(); ++i) {
        const auto& info = device.parameters[i];
        PluginParameterConfigEntry entry;
        entry.index = static_cast<int>(i);
        entry.name = info.name;
        entry.unit = info.unit;
        entry.scale = info.scale;
        entry.rangeMin = info.minValue;
        entry.rangeMax = info.maxValue;
        entry.rangeCenter = (info.minValue + info.maxValue) * 0.5f;
        if (!info.choices.empty())
            entry.choices = info.choices;
        if (!info.valueTable.empty())
            entry.valueTable = info.valueTable;
        config.entries.push_back(std::move(entry));
    }
    return config;
}

bool applyToDevice(const juce::String& uniqueId, DeviceInfo& device) {
    const auto config = load(uniqueId);
    if (!config)
        return false;

    device.visibleParameters.clear();
    device.miniMixerParameters.clear();
    device.aiSoundDesignerParameters.clear();
    device.aiSoundDesignerPrompt = config->aiPrompt;

    // device.parameters holds only the plugin's own params — TE's slot dry/wet
    // live in device.wrapperParameters — so a stored index maps 1:1 to the
    // device array. (Configs saved before the wrapper-param split assumed
    // indices 0/1 were dry/wet; those resolve to the wrong slots once and need
    // to be re-saved.)
    const auto count = static_cast<int>(device.parameters.size());
    for (const auto& entry : config->entries) {
        if (entry.index < 0 || entry.index >= count)
            continue;
        if (entry.visible)
            device.visibleParameters.push_back(entry.index);
        if (entry.miniMixer)
            device.miniMixerParameters.push_back(entry.index);
        if (entry.aiAgent)
            device.aiSoundDesignerParameters.push_back(entry.index);

        auto& parameter = device.parameters[static_cast<size_t>(entry.index)];
        if (entry.unit)
            parameter.unit = *entry.unit;
        if (entry.scale)
            parameter.scale = *entry.scale;
        if (entry.rangeMin)
            parameter.minValue = *entry.rangeMin;
        if (entry.rangeMax)
            parameter.maxValue = *entry.rangeMax;
        if (entry.choices)
            parameter.choices = *entry.choices;
        if (entry.valueTable)
            parameter.valueTable = *entry.valueTable;
    }
    return true;
}

bool hasAiSoundDesignerParameters(const juce::String& uniqueId) {
    const auto config = load(uniqueId);
    if (!config)
        return false;
    return std::any_of(config->entries.begin(), config->entries.end(),
                       [](const PluginParameterConfigEntry& entry) { return entry.aiAgent; });
}

void refreshLiveDevices(const juce::String& uniqueId) {
    if (uniqueId.isEmpty())
        return;

    auto& tm = TrackManager::getInstance();
    std::vector<TrackId> trackIds;
    trackIds.reserve(tm.getTracks().size() + 1);
    trackIds.push_back(MASTER_TRACK_ID);
    for (const auto& track : tm.getTracks())
        trackIds.push_back(track.id);

    for (auto trackId : trackIds) {
        auto* track = tm.getTrack(trackId);
        if (track == nullptr)
            continue;

        bool changed = refreshElementParameterConfig(uniqueId, track->chain.fxChainElements);
        changed = refreshFlatParameterConfig(uniqueId, track->chain.postFxChainElements) || changed;
        changed =
            refreshFlatParameterConfig(uniqueId, track->chain.mixerAnalysisElements) || changed;
        if (changed)
            tm.notifyTrackDevicesChanged(trackId);
    }
}

}  // namespace magda::PluginParameterConfigStore
