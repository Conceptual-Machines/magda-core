#include "Config.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>

// ---------------------------------------------------------------------------
// Path helper
// ---------------------------------------------------------------------------

namespace {
juce::File getConfigFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("config.json");
}
}  // namespace

namespace {
// Use fromUTF8 to avoid juce_String.cpp:327 assertion when std::string contains non-ASCII bytes
juce::String toJuceString(const std::string& s) {
    return juce::String::fromUTF8(s.c_str(), static_cast<int>(s.size()));
}
}  // namespace

namespace magda {

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

void Config::addRecentProject(const std::string& path) {
    // Remove existing entry if present (dedup)
    recentProjects.erase(std::remove(recentProjects.begin(), recentProjects.end(), path),
                         recentProjects.end());
    // Prepend
    recentProjects.insert(recentProjects.begin(), path);
    // Cap at 10
    if (recentProjects.size() > 10)
        recentProjects.resize(10);
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void Config::save() {
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());

    // Timeline (bars)
    root->setProperty("defaultTimelineLengthBars", defaultTimelineLengthBars);
    root->setProperty("defaultZoomViewBars", defaultZoomViewBars);

    // Zoom limits
    root->setProperty("minZoomLevel", minZoomLevel);
    root->setProperty("maxZoomLevel", maxZoomLevel);

    // Zoom sensitivity
    root->setProperty("zoomInSensitivity", zoomInSensitivity);
    root->setProperty("zoomOutSensitivity", zoomOutSensitivity);
    root->setProperty("zoomInSensitivityShift", zoomInSensitivityShift);
    root->setProperty("zoomOutSensitivityShift", zoomOutSensitivityShift);

    // Transport
    root->setProperty("transportShowBothFormats", transportShowBothFormats);
    root->setProperty("transportDefaultBarsBeats", transportDefaultBarsBeats);

    // Panel visibility
    root->setProperty("showLeftPanel", showLeftPanel);
    root->setProperty("showRightPanel", showRightPanel);
    root->setProperty("showBottomPanel", showBottomPanel);

    // Panel collapse state
    root->setProperty("leftPanelCollapsed", leftPanelCollapsed);
    root->setProperty("rightPanelCollapsed", rightPanelCollapsed);
    root->setProperty("bottomPanelCollapsed", bottomPanelCollapsed);

    // Panel sizes
    root->setProperty("leftPanelWidth", leftPanelWidth);
    root->setProperty("rightPanelWidth", rightPanelWidth);
    root->setProperty("bottomPanelHeight", bottomPanelHeight);

    // UI / behaviour
    root->setProperty("scrollbarOnLeft", scrollbarOnLeft);
    root->setProperty("confirmTrackDelete", confirmTrackDelete);
    root->setProperty("showTooltips", showTooltips);
    root->setProperty("autoMonitorSelectedTrack", autoMonitorSelectedTrack);
    root->setProperty("previewOutputChannel", previewOutputChannel);

    // Auto-save
    root->setProperty("autoSaveEnabled", autoSaveEnabled);
    root->setProperty("autoSaveIntervalSeconds", autoSaveIntervalSeconds);

    // Export audio
    root->setProperty("exportFormat", toJuceString(exportFormat));
    root->setProperty("exportSampleRate", exportSampleRate);

    // Render
    root->setProperty("renderFolder", toJuceString(renderFolder));
    root->setProperty("renderSampleRate", renderSampleRate);
    root->setProperty("renderBitDepth", renderBitDepth);
    root->setProperty("renderFilePattern", toJuceString(renderFilePattern));
    root->setProperty("bounceFilePattern", toJuceString(bounceFilePattern));
    root->setProperty("bounceBitDepth", bounceBitDepth);

    // Audio devices
    root->setProperty("preferredAudioDevice", toJuceString(preferredAudioDevice));
    root->setProperty("preferredInputDevice", toJuceString(preferredInputDevice));
    root->setProperty("preferredOutputDevice", toJuceString(preferredOutputDevice));
    root->setProperty("preferredInputChannels", preferredInputChannels);
    root->setProperty("preferredOutputChannels", preferredOutputChannels);

    // AI — nested "ai" object with per-agent configs
    {
        auto* aiObj = new juce::DynamicObject();
        aiObj->setProperty("preset", toJuceString(aiPreset));

        auto* agentsObj = new juce::DynamicObject();
        for (const auto& [role, cfg] : agentConfigs) {
            auto* agentObj = new juce::DynamicObject();
            agentObj->setProperty("provider", toJuceString(cfg.provider));
            agentObj->setProperty("baseUrl", toJuceString(cfg.baseUrl));
            agentObj->setProperty("apiKey", toJuceString(cfg.apiKey));
            agentObj->setProperty("model", toJuceString(cfg.model));
            agentsObj->setProperty(juce::String(role), juce::var(agentObj));
        }
        aiObj->setProperty("agents", juce::var(agentsObj));

        auto* credsObj = new juce::DynamicObject();
        for (const auto& [provider, key] : aiCredentials) {
            credsObj->setProperty(juce::String(provider), toJuceString(key));
        }
        aiObj->setProperty("credentials", juce::var(credsObj));
        aiObj->setProperty("localLlamaUrl", toJuceString(localLlamaUrl));
        aiObj->setProperty("localModelPath", toJuceString(localModelPath));
        aiObj->setProperty("localLlamaBinary", toJuceString(localLlamaBinary));
        aiObj->setProperty("localLlamaPort", localLlamaPort);
        aiObj->setProperty("localLlamaGpuLayers", localLlamaGpuLayers);
        aiObj->setProperty("localLlamaContextSize", localLlamaContextSize);

        root->setProperty("ai", juce::var(aiObj));
    }

    // Browser
    root->setProperty("browserFilterAudio", browserFilterAudio);
    root->setProperty("browserFilterMidi", browserFilterMidi);
    root->setProperty("browserDefaultDirectory", toJuceString(browserDefaultDirectory));

    juce::Array<juce::var> favArray;
    for (const auto& f : browserFavorites)
        favArray.add(toJuceString(f));
    root->setProperty("browserFavorites", favArray);

    // Recent projects
    juce::Array<juce::var> recentArray;
    for (const auto& r : recentProjects)
        recentArray.add(toJuceString(r));
    root->setProperty("recentProjects", recentArray);

    // Custom plugin paths
    juce::Array<juce::var> pluginPathArray;
    for (const auto& p : customPluginPaths)
        pluginPathArray.add(toJuceString(p));
    root->setProperty("customPluginPaths", pluginPathArray);

    // Total plugin count
    root->setProperty("totalPluginCount", totalPluginCount);
    root->setProperty("scanPluginsOnStartup", scanPluginsOnStartup);
    root->setProperty("loadModelOnStartup", loadModelOnStartup);

    // Clip colour mode
    root->setProperty("clipColourMode", clipColourMode);

    // Track colour palette (stored as array of {colour, name} objects)
    juce::Array<juce::var> paletteArray;
    for (const auto& entry : trackColourPalette) {
        auto entryObj = juce::DynamicObject::Ptr(new juce::DynamicObject());
        entryObj->setProperty("colour", juce::String::toHexString(static_cast<int>(entry.colour))
                                            .paddedLeft('0', 8)
                                            .toUpperCase());
        entryObj->setProperty("name", toJuceString(entry.name));
        paletteArray.add(juce::var(entryObj.get()));
    }
    root->setProperty("trackColourPalette", paletteArray);

    // Write to disk
    auto configFile = getConfigFile();
    configFile.getParentDirectory().createDirectory();

    auto json = juce::JSON::toString(juce::var(root.get()));
    if (!configFile.replaceWithText(json))
        DBG("Config::save - failed to write " + configFile.getFullPathName());
    else
        DBG("Config::save - " + configFile.getFullPathName());

    auto listenersCopy = listeners_;
    for (auto* l : listenersCopy)
        if (l != nullptr)
            l->configChanged();
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

void Config::load() {
    auto configFile = getConfigFile();
    if (!configFile.existsAsFile()) {
        DBG("Config::load - file not found, using defaults: " + configFile.getFullPathName());
        return;
    }

    juce::var parsed;
    auto result = juce::JSON::parse(configFile.loadFileAsString(), parsed);
    if (result.failed()) {
        DBG("Config::load - JSON parse error: " + result.getErrorMessage());
        return;
    }

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        DBG("Config::load - unexpected JSON root type");
        return;
    }

    auto getDouble = [&](const char* key, double fallback) -> double {
        if (!obj->hasProperty(key))
            return fallback;
        return static_cast<double>(obj->getProperty(key));
    };
    auto getBool = [&](const char* key, bool fallback) -> bool {
        if (!obj->hasProperty(key))
            return fallback;
        return static_cast<bool>(obj->getProperty(key));
    };
    auto getInt = [&](const char* key, int fallback) -> int {
        if (!obj->hasProperty(key))
            return fallback;
        return static_cast<int>(obj->getProperty(key));
    };
    auto getString = [&](const char* key, const std::string& fallback) -> std::string {
        if (!obj->hasProperty(key))
            return fallback;
        return obj->getProperty(key).toString().toStdString();
    };
    auto getStringArray = [&](const char* key) -> std::vector<std::string> {
        std::vector<std::string> out;
        if (!obj->hasProperty(key))
            return out;
        const auto& v = obj->getProperty(key);
        if (v.isArray()) {
            for (const auto& item : *v.getArray())
                out.push_back(item.toString().toStdString());
        }
        return out;
    };

    // Migrate from old seconds-based keys if new bars keys don't exist yet
    if (obj->hasProperty("defaultTimelineLengthBars")) {
        defaultTimelineLengthBars = getInt("defaultTimelineLengthBars", defaultTimelineLengthBars);
    } else if (obj->hasProperty("defaultTimelineLength")) {
        // Old config: convert seconds to bars (at 120 BPM, 4/4: 1 bar = 2 sec)
        defaultTimelineLengthBars =
            static_cast<int>(getDouble("defaultTimelineLength", 256.0) / 2.0);
    }
    if (obj->hasProperty("defaultZoomViewBars")) {
        defaultZoomViewBars = getInt("defaultZoomViewBars", defaultZoomViewBars);
    } else if (obj->hasProperty("defaultZoomViewDuration")) {
        defaultZoomViewBars = static_cast<int>(getDouble("defaultZoomViewDuration", 64.0) / 2.0);
    }
    minZoomLevel = getDouble("minZoomLevel", minZoomLevel);
    maxZoomLevel = getDouble("maxZoomLevel", maxZoomLevel);

    zoomInSensitivity = getDouble("zoomInSensitivity", zoomInSensitivity);
    zoomOutSensitivity = getDouble("zoomOutSensitivity", zoomOutSensitivity);
    zoomInSensitivityShift = getDouble("zoomInSensitivityShift", zoomInSensitivityShift);
    zoomOutSensitivityShift = getDouble("zoomOutSensitivityShift", zoomOutSensitivityShift);

    transportShowBothFormats = getBool("transportShowBothFormats", transportShowBothFormats);
    transportDefaultBarsBeats = getBool("transportDefaultBarsBeats", transportDefaultBarsBeats);

    showLeftPanel = getBool("showLeftPanel", showLeftPanel);
    showRightPanel = getBool("showRightPanel", showRightPanel);
    showBottomPanel = getBool("showBottomPanel", showBottomPanel);

    leftPanelCollapsed = getBool("leftPanelCollapsed", leftPanelCollapsed);
    rightPanelCollapsed = getBool("rightPanelCollapsed", rightPanelCollapsed);
    bottomPanelCollapsed = getBool("bottomPanelCollapsed", bottomPanelCollapsed);

    leftPanelWidth = getInt("leftPanelWidth", leftPanelWidth);
    rightPanelWidth = getInt("rightPanelWidth", rightPanelWidth);
    bottomPanelHeight = getInt("bottomPanelHeight", bottomPanelHeight);

    scrollbarOnLeft = getBool("scrollbarOnLeft", scrollbarOnLeft);
    confirmTrackDelete = getBool("confirmTrackDelete", confirmTrackDelete);
    showTooltips = getBool("showTooltips", showTooltips);
    autoMonitorSelectedTrack = getBool("autoMonitorSelectedTrack", autoMonitorSelectedTrack);
    previewOutputChannel = getInt("previewOutputChannel", previewOutputChannel);

    autoSaveEnabled = getBool("autoSaveEnabled", autoSaveEnabled);
    autoSaveIntervalSeconds = getInt("autoSaveIntervalSeconds", autoSaveIntervalSeconds);

    exportFormat = getString("exportFormat", exportFormat);
    exportSampleRate = getDouble("exportSampleRate", exportSampleRate);

    renderFolder = getString("renderFolder", renderFolder);
    renderSampleRate = getDouble("renderSampleRate", renderSampleRate);
    renderBitDepth = getInt("renderBitDepth", renderBitDepth);
    renderFilePattern = getString("renderFilePattern", renderFilePattern);
    bounceFilePattern = getString("bounceFilePattern", bounceFilePattern);
    bounceBitDepth = getInt("bounceBitDepth", bounceBitDepth);

    preferredAudioDevice = getString("preferredAudioDevice", preferredAudioDevice);
    preferredInputDevice = getString("preferredInputDevice", preferredInputDevice);
    preferredOutputDevice = getString("preferredOutputDevice", preferredOutputDevice);
    preferredInputChannels = getInt("preferredInputChannels", preferredInputChannels);
    preferredOutputChannels = getInt("preferredOutputChannels", preferredOutputChannels);

    // AI — load nested "ai" object, or migrate from legacy flat fields
    if (obj->hasProperty("ai")) {
        auto aiVar = obj->getProperty("ai");
        if (auto* aiObj = aiVar.getDynamicObject()) {
            aiPreset = aiObj->getProperty("preset").toString().toStdString();
            auto agentsVar = aiObj->getProperty("agents");
            if (auto* agentsObj = agentsVar.getDynamicObject()) {
                agentConfigs.clear();
                for (const auto& prop : agentsObj->getProperties()) {
                    auto role = prop.name.toString().toStdString();
                    if (auto* agentObj = prop.value.getDynamicObject()) {
                        AgentLLMConfig cfg;
                        cfg.provider = agentObj->getProperty("provider").toString().toStdString();
                        cfg.baseUrl = agentObj->getProperty("baseUrl").toString().toStdString();
                        cfg.apiKey = agentObj->getProperty("apiKey").toString().toStdString();
                        cfg.model = agentObj->getProperty("model").toString().toStdString();
                        // Migrate: openai_chat + deepseek/openrouter baseUrl → own provider
                        if (cfg.provider == "openai_chat" && !cfg.baseUrl.empty()) {
                            if (cfg.baseUrl.find("deepseek") != std::string::npos) {
                                cfg.provider = "deepseek";
                                cfg.baseUrl.clear();
                            } else if (cfg.baseUrl.find("openrouter") != std::string::npos) {
                                cfg.provider = "openrouter";
                                cfg.baseUrl.clear();
                            }
                        }

                        agentConfigs[role] = cfg;
                    }
                }
            }

            // Local llama settings
            if (aiObj->hasProperty("localLlamaUrl"))
                localLlamaUrl = aiObj->getProperty("localLlamaUrl").toString().toStdString();
            if (aiObj->hasProperty("localModelPath"))
                localModelPath = aiObj->getProperty("localModelPath").toString().toStdString();
            if (aiObj->hasProperty("localLlamaBinary"))
                localLlamaBinary = aiObj->getProperty("localLlamaBinary").toString().toStdString();
            if (aiObj->hasProperty("localLlamaPort"))
                localLlamaPort = static_cast<int>(aiObj->getProperty("localLlamaPort"));
            if (aiObj->hasProperty("localLlamaGpuLayers"))
                localLlamaGpuLayers = static_cast<int>(aiObj->getProperty("localLlamaGpuLayers"));
            if (aiObj->hasProperty("localLlamaContextSize"))
                localLlamaContextSize =
                    static_cast<int>(aiObj->getProperty("localLlamaContextSize"));

            // Migrate: openai_chat + GPT-5 → openai_responses (older configs used wrong provider)
            for (auto& [role, cfg] : agentConfigs) {
                if (cfg.provider == "openai_chat" && cfg.baseUrl.empty()) {
                    if (juce::String(cfg.model).startsWith("gpt-5")) {
                        cfg.provider = "openai_responses";
                    } else if (role == "command" || role == "music") {
                        // Older configs had command/music on gpt-4.1-mini — upgrade
                        cfg.provider = "openai_responses";
                        cfg.model = "gpt-5";
                    }
                }
            }

            // Load per-provider credentials
            auto credsVar = aiObj->getProperty("credentials");
            if (auto* credsObj = credsVar.getDynamicObject()) {
                aiCredentials.clear();
                for (const auto& prop : credsObj->getProperties()) {
                    auto provider = prop.name.toString().toStdString();
                    auto key = prop.value.toString().toStdString();
                    if (!key.empty())
                        aiCredentials[provider] = key;
                }
            }
        }
    } else {
        // Migrate from legacy flat fields
        AgentLLMConfig musicCfg;
        musicCfg.provider = getString("llmProvider", "openai_chat");
        musicCfg.baseUrl = getString("llmBaseUrl", "");
        musicCfg.model = getString("llmModel", "gpt-4.1");
        musicCfg.apiKey = getString("llmApiKey", "");
        // Try legacy OpenAI key
        if (musicCfg.apiKey.empty())
            musicCfg.apiKey = getString("openaiApiKey", "");
        if (!musicCfg.model.empty() && musicCfg.model == "gpt-4.1")
            musicCfg.model = getString("openaiModel", musicCfg.model);

        // Only upgrade OpenAI-flavored legacy configs to Responses/GPT-5.
        // Non-OpenAI providers (anthropic, gemini, deepseek, openrouter, llama_local) are
        // preserved as-is so the user's prior provider, auth, and model continue to work.
        const bool isLegacyOpenAI = musicCfg.provider == "openai" ||
                                    musicCfg.provider == "openai_chat" ||
                                    musicCfg.provider == "openai_responses";

        if (isLegacyOpenAI && musicCfg.baseUrl.empty()) {
            musicCfg.provider = "openai_responses";
            if (!juce::String(musicCfg.model).startsWith("gpt-5"))
                musicCfg.model = "gpt-5";
        }
        agentConfigs["music"] = musicCfg;

        AgentLLMConfig commandCfg = musicCfg;
        agentConfigs["command"] = commandCfg;

        AgentLLMConfig routerCfg;
        if (isLegacyOpenAI && musicCfg.baseUrl.empty()) {
            routerCfg.provider = "openai_chat";
            routerCfg.model = "gpt-4.1-mini";
            routerCfg.apiKey = musicCfg.apiKey;
        } else {
            // Mirror the user's existing provider for the router too
            routerCfg = musicCfg;
        }
        agentConfigs["router"] = routerCfg;
    }

    browserFilterAudio = getBool("browserFilterAudio", browserFilterAudio);
    browserFilterMidi = getBool("browserFilterMidi", browserFilterMidi);
    browserDefaultDirectory = getString("browserDefaultDirectory", browserDefaultDirectory);
    browserFavorites = getStringArray("browserFavorites");
    recentProjects = getStringArray("recentProjects");
    customPluginPaths = getStringArray("customPluginPaths");
    totalPluginCount = getInt("totalPluginCount", totalPluginCount);
    scanPluginsOnStartup = getBool("scanPluginsOnStartup", scanPluginsOnStartup);
    loadModelOnStartup = getBool("loadModelOnStartup", loadModelOnStartup);

    clipColourMode = getInt("clipColourMode", clipColourMode);

    // Track colour palette
    if (obj->hasProperty("trackColourPalette")) {
        const auto& v = obj->getProperty("trackColourPalette");
        if (v.isArray()) {
            trackColourPalette.clear();
            for (const auto& item : *v.getArray()) {
                if (auto* entryObj = item.getDynamicObject()) {
                    TrackColourEntry entry;
                    entry.colour = static_cast<uint32_t>(
                        entryObj->getProperty("colour").toString().getHexValue64());
                    entry.name = entryObj->getProperty("name").toString().toStdString();
                    trackColourPalette.push_back(entry);
                }
            }
        }
    }

    DBG("Config::load - " + configFile.getFullPathName());
}

}  // namespace magda
