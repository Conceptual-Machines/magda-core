#include "Config.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>

#include "AppPaths.hpp"
#include "LLMClientProvider.hpp"

// ---------------------------------------------------------------------------
// Path helper
// ---------------------------------------------------------------------------
// config.json itself is anchored to the OS default location (paths::configFile
// returns alwaysOSDefault() / "config.json"). It cannot move with the
// configurable data dir override — Config has to be loaded BEFORE the
// override is known.

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
    root->setProperty("openPluginWindowOnDrop", openPluginWindowOnDrop);
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

    // Language
    root->setProperty("language", toJuceString(language));

    // UI / behaviour
    root->setProperty("scrollbarOnLeft", scrollbarOnLeft);
    root->setProperty("arrangementScrollbarsAutoHide", arrangementScrollbarsAutoHide);
    root->setProperty("uiScale", uiScale);
    root->setProperty("theme", toJuceString(theme));
    root->setProperty("uiDensityScale", uiDensityScale);
    root->setProperty("uiFontScale", uiFontScale);
    root->setProperty("uiFontFamily", toJuceString(uiFontFamily));
    root->setProperty("localizedUIFontScale", localizedUIFontScale);
    root->setProperty("confirmTrackDelete", confirmTrackDelete);
    root->setProperty("duplicateLoopGrows", duplicateLoopGrows);
    root->setProperty("showTooltips", showTooltips);
    root->setProperty("autoMonitorSelectedTrack", autoMonitorSelectedTrack);
    root->setProperty("openMacrosOnSelect", openMacrosOnSelect);

    // Mixer view-toggle rail
    root->setProperty("mixerShowSends", mixerShowSends_);
    root->setProperty("mixerShowRouting", mixerShowRouting_);
    root->setProperty("mixerShowMonitor", mixerShowMonitor_);
    root->setProperty("mixerShowOscilloscope", mixerShowOscilloscope_);
    root->setProperty("mixerShowSpectrum", mixerShowSpectrum_);
    root->setProperty("mixerShowFxChain", mixerShowFxChain_);
    root->setProperty("sessionShowSends", sessionShowSends_);
    root->setProperty("sessionShowRouting", sessionShowRouting_);
    root->setProperty("sessionShowMonitor", sessionShowMonitor_);
    root->setProperty("persistMixerAnalysis", persistMixerAnalysis_);
    root->setProperty("previewOutputChannel", previewOutputChannel);

    // Auto-save
    root->setProperty("autoSaveEnabled", autoSaveEnabled);
    root->setProperty("autoSaveIntervalSeconds", autoSaveIntervalSeconds);

    // Export audio
    root->setProperty("exportFormat", toJuceString(exportFormat));
    root->setProperty("exportSampleRate", exportSampleRate);

    // Configurable user-data paths (empty string = OS default; resolved by
    // magda::paths::resolve()). Render folder uses the same field as before.
    root->setProperty("dataDir", toJuceString(dataDir));
    root->setProperty("presetsDir", toJuceString(presetsDir));

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

    // AI — nested "ai" object with per-agent inference profiles.
    {
        auto* aiObj = new juce::DynamicObject();
        aiObj->setProperty("preset", toJuceString(aiPreset));

        auto* agentsObj = new juce::DynamicObject();
        for (const auto& [role, profile] : agentInferenceConfigs) {
            auto* agentObj = new juce::DynamicObject();
            agentObj->setProperty("backend", toJuceString(profile.backend));
            auto* llmObj = new juce::DynamicObject();
            llmObj->setProperty("provider", toJuceString(profile.llm.provider));
            llmObj->setProperty("baseUrl", toJuceString(profile.llm.baseUrl));
            llmObj->setProperty("apiKey", toJuceString(profile.llm.apiKey));
            llmObj->setProperty("model", toJuceString(profile.llm.model));
            agentObj->setProperty("llm", juce::var(llmObj));
            agentsObj->setProperty(juce::String(role), juce::var(agentObj));
        }
        aiObj->setProperty("agents", juce::var(agentsObj));

        auto* credsObj = new juce::DynamicObject();
        for (const auto& [provider, key] : aiCredentials) {
            credsObj->setProperty(juce::String(provider), toJuceString(key));
        }
        aiObj->setProperty("credentials", juce::var(credsObj));
        aiObj->setProperty("localLlamaUrl", toJuceString(localLlamaUrl));
        aiObj->setProperty("localServerUrl", toJuceString(localServerUrl));
        aiObj->setProperty("localServerApiKey", toJuceString(localServerApiKey));
        aiObj->setProperty("localServerModel", toJuceString(localServerModel));
        aiObj->setProperty("localModelPath", toJuceString(localModelPath));
        aiObj->setProperty("localLlamaBinary", toJuceString(localLlamaBinary));
        aiObj->setProperty("localLlamaPort", localLlamaPort);
        aiObj->setProperty("localLlamaGpuLayers", localLlamaGpuLayers);
        aiObj->setProperty("localLlamaContextSize", localLlamaContextSize);

        // MCP servers
        if (!mcpServers.empty()) {
            juce::Array<juce::var> mcpArray;
            for (const auto& srv : mcpServers) {
                auto* srvObj = new juce::DynamicObject();
                srvObj->setProperty("name", toJuceString(srv.name));
                srvObj->setProperty("command", toJuceString(srv.command));
                juce::Array<juce::var> argsArray;
                for (const auto& a : srv.args)
                    argsArray.add(toJuceString(a));
                srvObj->setProperty("args", argsArray);
                srvObj->setProperty("enabled", srv.enabled);
                mcpArray.add(juce::var(srvObj));
            }
            aiObj->setProperty("mcpServers", mcpArray);
        }

        root->setProperty("ai", juce::var(aiObj));
    }

    // Browser
    root->setProperty("browserFilterAudio", browserFilterAudio);
    root->setProperty("browserFilterMidi", browserFilterMidi);
    root->setProperty("browserFilterPreset", browserFilterPreset);
    root->setProperty("browserTreeView", browserTreeView);
    root->setProperty("browserDefaultDirectory", toJuceString(browserDefaultDirectory));
    root->setProperty("browserLastView", toJuceString(browserLastView));
    root->setProperty("sampleTaggerModelsDir", toJuceString(sampleTaggerModelsDir));
    root->setProperty("commandModelModelsDir", toJuceString(commandModelModelsDir));
    root->setProperty("loadSampleTaggerOnStartup", loadSampleTaggerOnStartup);
    root->setProperty("mediaDbDir", toJuceString(mediaDbDir));
    root->setProperty("externalAudioEditorPath", toJuceString(externalAudioEditorPath));

    juce::Array<juce::var> favArray;
    for (const auto& f : browserFavorites)
        favArray.add(toJuceString(f));
    root->setProperty("browserFavorites", favArray);

    // Auto-update check
    root->setProperty("autoCheckUpdates", autoCheckUpdates);
    root->setProperty("lastUpdateCheckTimestamp",
                      static_cast<juce::int64>(lastUpdateCheckTimestamp));

    // Remote API — nested "remoteApi" object. No token here by design; it is
    // generated per run into a file only the user can read.
    {
        auto* remoteObj = new juce::DynamicObject();
        remoteObj->setProperty("enabled", remoteApiEnabled);
        remoteObj->setProperty("port", remoteApiPort);
        remoteObj->setProperty("mcpPort", remoteApiMcpPort);
        juce::Array<juce::var> originArray;
        for (const auto& origin : remoteApiAllowedOrigins)
            originArray.add(toJuceString(origin));
        remoteObj->setProperty("allowedOrigins", originArray);
        // Per-client grants (#1860). Written only once something has been
        // granted, so an install that never used the remote API keeps a config
        // file without an empty array in it.
        if (remoteApiClients.isArray() && remoteApiClients.getArray()->size() > 0)
            remoteObj->setProperty("clients", remoteApiClients);
        root->setProperty("remoteApi", juce::var(remoteObj));
    }

    // OSC control surfaces — nested "osc" object (#1757).
    {
        auto* oscObj = new juce::DynamicObject();
        oscObj->setProperty("enabled", oscEnabled);
        oscObj->setProperty("receivePort", oscReceivePort);
        oscObj->setProperty("bindAddress", toJuceString(oscBindAddress));
        root->setProperty("osc", juce::var(oscObj));
    }

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

    // External-insert auto-enabled hardware ports
    juce::Array<juce::var> autoInsertInArray;
    for (const auto& n : autoEnabledInsertInputs_)
        autoInsertInArray.add(toJuceString(n));
    root->setProperty("autoEnabledInsertInputs", autoInsertInArray);
    juce::Array<juce::var> autoInsertOutArray;
    for (const auto& n : autoEnabledInsertOutputs_)
        autoInsertOutArray.add(toJuceString(n));
    root->setProperty("autoEnabledInsertOutputs", autoInsertOutArray);

    // Total plugin count
    root->setProperty("totalPluginCount", totalPluginCount);
    root->setProperty("scanPluginsOnStartup", scanPluginsOnStartup);
    root->setProperty("loadModelOnStartup", loadModelOnStartup);
    root->setProperty("stopUpdatesPlayhead", stopUpdatesPlayhead);
    root->setProperty("followPlayhead", followPlayhead);
    root->setProperty("chordPreviewOnByDefault", chordPreviewOnByDefault);
    root->setProperty("autoCrossfadeByDefault", autoCrossfadeByDefault);
    root->setProperty("clipOverlapPlaysBoth", clipOverlapPlaysBoth);

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

    // Parameter aliases (user-global layer)
    if (!paramAliases_.isVoid())
        root->setProperty("paramAliases", paramAliases_);

    // Controller devices
    if (!controllers_.isVoid())
        root->setProperty("controllers", controllers_);

    // Lua controller scripts
    if (!luaScripts_.isVoid())
        root->setProperty("luaScripts", luaScripts_);
    if (!activeLuaScript_.empty())
        root->setProperty("activeLuaScript", toJuceString(activeLuaScript_));
    if (!enabledFactoryLuaScripts_.empty()) {
        juce::Array<juce::var> arr;
        for (const auto& name : enabledFactoryLuaScripts_)
            arr.add(toJuceString(name));
        root->setProperty("enabledFactoryLuaScripts", arr);
    }

    // Global bindings
    if (!globalBindings_.isVoid())
        root->setProperty("globalBindings", globalBindings_);

    // Keyboard-shortcut overrides (#20) and mouse-gesture overrides (#21)
    if (!keyboardBindings_.isVoid())
        root->setProperty("keyboardBindings", keyboardBindings_);
    if (!gestureBindings_.isVoid())
        root->setProperty("gestureBindings", gestureBindings_);

    // MIDI Learn settings
    {
        auto* mlObj = new juce::DynamicObject();
        mlObj->setProperty("defaultScope", midiLearnDefaultScope_ == 0 ? juce::String("global")
                                                                       : juce::String("project"));
        root->setProperty("midiLearn", juce::var(mlObj));
    }

    // Analysis device defaults (last-used settings for new osc / spectrum)
    {
        auto* adObj = new juce::DynamicObject();

        auto* oscObj = new juce::DynamicObject();
        oscObj->setProperty("timebaseMs", oscilloscopeDefaults_.timebaseMs);
        adObj->setProperty("oscilloscope", juce::var(oscObj));

        auto* specObj = new juce::DynamicObject();
        specObj->setProperty("fftOrder", spectrumDefaults_.fftOrder);
        specObj->setProperty("slopeDbPerOct", spectrumDefaults_.slopeDbPerOct);
        specObj->setProperty("smoothing", spectrumDefaults_.smoothing);
        adObj->setProperty("spectrum", juce::var(specObj));

        root->setProperty("analysisDefaults", juce::var(adObj));
    }

    // Write to disk
    auto configFile = magda::paths::configFile();
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
    auto configFile = magda::paths::configFile();
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

    defaultTimelineLengthBars = getInt("defaultTimelineLengthBars", defaultTimelineLengthBars);
    defaultZoomViewBars = getInt("defaultZoomViewBars", defaultZoomViewBars);
    minZoomLevel = getDouble("minZoomLevel", minZoomLevel);
    maxZoomLevel = getDouble("maxZoomLevel", maxZoomLevel);

    zoomInSensitivity = getDouble("zoomInSensitivity", zoomInSensitivity);
    zoomOutSensitivity = getDouble("zoomOutSensitivity", zoomOutSensitivity);
    zoomInSensitivityShift = getDouble("zoomInSensitivityShift", zoomInSensitivityShift);
    zoomOutSensitivityShift = getDouble("zoomOutSensitivityShift", zoomOutSensitivityShift);

    openPluginWindowOnDrop = getBool("openPluginWindowOnDrop", openPluginWindowOnDrop);
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

    language = getString("language", language);
    scrollbarOnLeft = getBool("scrollbarOnLeft", scrollbarOnLeft);
    arrangementScrollbarsAutoHide =
        getBool("arrangementScrollbarsAutoHide", arrangementScrollbarsAutoHide);
    uiScale = getDouble("uiScale", uiScale);
    setTheme(getString("theme", theme));
    setUIDensityScale(getDouble("uiDensityScale", uiDensityScale));
    setUIFontScale(getDouble("uiFontScale", uiFontScale));
    setUIFontFamily(getString("uiFontFamily", uiFontFamily));
    localizedUIFontScaleExplicit = obj->hasProperty("localizedUIFontScale");
    if (localizedUIFontScaleExplicit)
        setLocalizedUIFontScale(getDouble("localizedUIFontScale", localizedUIFontScale));
    else
        localizedUIFontScale = defaultLocalizedUIFontScaleForLanguage(juce::String(language));
    confirmTrackDelete = getBool("confirmTrackDelete", confirmTrackDelete);
    duplicateLoopGrows = getBool("duplicateLoopGrows", duplicateLoopGrows);
    showTooltips = getBool("showTooltips", showTooltips);
    autoMonitorSelectedTrack = getBool("autoMonitorSelectedTrack", autoMonitorSelectedTrack);
    openMacrosOnSelect = getBool("openMacrosOnSelect", openMacrosOnSelect);

    mixerShowSends_ = getBool("mixerShowSends", mixerShowSends_);
    mixerShowRouting_ = getBool("mixerShowRouting", mixerShowRouting_);
    mixerShowMonitor_ = getBool("mixerShowMonitor", mixerShowMonitor_);
    mixerShowOscilloscope_ = getBool("mixerShowOscilloscope", mixerShowOscilloscope_);
    mixerShowSpectrum_ = getBool("mixerShowSpectrum", mixerShowSpectrum_);
    mixerShowFxChain_ = getBool("mixerShowFxChain", mixerShowFxChain_);
    sessionShowSends_ = getBool("sessionShowSends", sessionShowSends_);
    sessionShowRouting_ = getBool("sessionShowRouting", sessionShowRouting_);
    sessionShowMonitor_ = getBool("sessionShowMonitor", sessionShowMonitor_);
    persistMixerAnalysis_ = getBool("persistMixerAnalysis", persistMixerAnalysis_);
    previewOutputChannel = getInt("previewOutputChannel", previewOutputChannel);

    autoSaveEnabled = getBool("autoSaveEnabled", autoSaveEnabled);
    autoSaveIntervalSeconds = getInt("autoSaveIntervalSeconds", autoSaveIntervalSeconds);

    exportFormat = getString("exportFormat", exportFormat);
    exportSampleRate = getDouble("exportSampleRate", exportSampleRate);

    dataDir = getString("dataDir", dataDir);
    presetsDir = getString("presetsDir", presetsDir);
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
                bool jsonHadController = false;
                bool jsonHadMusic = false;
                bool jsonHadFaust = false;
                bool jsonHadChord = false;
                bool jsonHadTheme = false;

                for (const auto& prop : agentsObj->getProperties()) {
                    auto role = prop.name.toString().toStdString();
                    // The model-based intent router was retired in favour of
                    // deterministic context surfaces. Drop stale on-disk rows
                    // so the next save removes the obsolete configuration.
                    if (role == "router")
                        continue;
                    if (auto* agentObj = prop.value.getDynamicObject()) {
                        AgentInferenceConfig profile;
                        profile.backend =
                            agentObj->hasProperty("backend")
                                ? agentObj->getProperty("backend").toString().toStdString()
                                : "llm";
                        auto llmVar = agentObj->getProperty("llm");
                        auto* llmObj = llmVar.getDynamicObject();
                        // Flat provider/baseUrl/apiKey/model is the pre-profile
                        // on-disk shape. Read it as the LLM backend payload.
                        auto& cfg = profile.llm;
                        const auto read = llmObj != nullptr ? llmObj : agentObj;
                        cfg.provider = read->getProperty("provider").toString().toStdString();
                        cfg.baseUrl = read->getProperty("baseUrl").toString().toStdString();
                        cfg.apiKey = read->getProperty("apiKey").toString().toStdString();
                        cfg.model = read->getProperty("model").toString().toStdString();
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

                        agentInferenceConfigs[role] = profile;
                        if (role == "controller")
                            jsonHadController = true;
                        if (role == "music")
                            jsonHadMusic = true;
                        if (role == "faust")
                            jsonHadFaust = true;
                        if (role == "chord")
                            jsonHadChord = true;
                        if (role == "theme")
                            jsonHadTheme = true;
                    }
                }

                // Saved configs that predate the "controller"/"theme" roles need
                // a live config. Clone music so the user's cloud setup carries
                // over instead of leaving the class-default llama_local in place.
                if (!jsonHadController && jsonHadMusic)
                    agentInferenceConfigs["controller"] = agentInferenceConfigs["music"];
                if (!jsonHadFaust && jsonHadMusic)
                    agentInferenceConfigs["faust"] = agentInferenceConfigs["music"];
                if (!jsonHadChord && jsonHadMusic)
                    agentInferenceConfigs["chord"] = agentInferenceConfigs["music"];
                if (!jsonHadTheme && jsonHadMusic)
                    agentInferenceConfigs["theme"] = agentInferenceConfigs["music"];
            }

            // Local llama settings
            if (aiObj->hasProperty("localLlamaUrl"))
                localLlamaUrl = aiObj->getProperty("localLlamaUrl").toString().toStdString();
            if (aiObj->hasProperty("localServerUrl"))
                localServerUrl = aiObj->getProperty("localServerUrl").toString().toStdString();
            if (aiObj->hasProperty("localServerApiKey"))
                localServerApiKey =
                    aiObj->getProperty("localServerApiKey").toString().toStdString();
            if (aiObj->hasProperty("localServerModel"))
                localServerModel = aiObj->getProperty("localServerModel").toString().toStdString();
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

            // Migrate: openai_chat + a Responses-only model (gpt-5*, o-series)
            // -> openai_responses (older configs used the wrong provider)
            for (auto& [role, profile] : agentInferenceConfigs) {
                if (!profile.usesLLM())
                    continue;
                auto& cfg = profile.llm;
                if (cfg.provider == "openai_chat" && cfg.baseUrl.empty()) {
                    if (requiresOpenAIResponsesAPI(juce::String(cfg.model))) {
                        cfg.provider = "openai_responses";
                    } else if (role == "command" || role == "music") {
                        // Older configs had command/music on gpt-4.1-mini — upgrade
                        cfg.provider = "openai_responses";
                        cfg.model = "gpt-5";
                    }
                }
            }

            // Load MCP server configs
            auto mcpVar = aiObj->getProperty("mcpServers");
            if (mcpVar.isArray()) {
                mcpServers.clear();
                for (const auto& item : *mcpVar.getArray()) {
                    if (auto* srvObj = item.getDynamicObject()) {
                        MCPServerConfig srv;
                        srv.name = srvObj->getProperty("name").toString().toStdString();
                        srv.command = srvObj->getProperty("command").toString().toStdString();
                        auto argsVar = srvObj->getProperty("args");
                        if (argsVar.isArray()) {
                            for (const auto& a : *argsVar.getArray())
                                srv.args.push_back(a.toString().toStdString());
                        }
                        srv.enabled = srvObj->hasProperty("enabled")
                                          ? static_cast<bool>(srvObj->getProperty("enabled"))
                                          : true;
                        if (!srv.name.empty() && !srv.command.empty())
                            mcpServers.push_back(std::move(srv));
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
        setAgentLLMConfig("music", musicCfg);
        setAgentLLMConfig("faust", musicCfg);
        setAgentLLMConfig("chord", musicCfg);

        AgentLLMConfig commandCfg = musicCfg;
        setAgentLLMConfig("command", commandCfg);

        AgentLLMConfig controllerCfg = musicCfg;
        setAgentLLMConfig("controller", controllerCfg);
    }

    browserFilterAudio = getBool("browserFilterAudio", browserFilterAudio);
    browserFilterMidi = getBool("browserFilterMidi", browserFilterMidi);
    browserFilterPreset = getBool("browserFilterPreset", browserFilterPreset);
    browserTreeView = getBool("browserTreeView", browserTreeView);
    browserDefaultDirectory = getString("browserDefaultDirectory", browserDefaultDirectory);
    browserLastView = getString("browserLastView", browserLastView);
    sampleTaggerModelsDir = getString("sampleTaggerModelsDir", sampleTaggerModelsDir);
    commandModelModelsDir = getString("commandModelModelsDir", commandModelModelsDir);
    loadSampleTaggerOnStartup = getBool("loadSampleTaggerOnStartup", loadSampleTaggerOnStartup);
    mediaDbDir = getString("mediaDbDir", mediaDbDir);
    externalAudioEditorPath = getString("externalAudioEditorPath", externalAudioEditorPath);
    browserFavorites = getStringArray("browserFavorites");

    // Auto-update check
    autoCheckUpdates = getBool("autoCheckUpdates", autoCheckUpdates);
    if (obj->hasProperty("lastUpdateCheckTimestamp"))
        lastUpdateCheckTimestamp = static_cast<int64_t>(
            static_cast<juce::int64>(obj->getProperty("lastUpdateCheckTimestamp")));

    // Remote API — absent means the defaults, which leave the listener off.
    if (obj->hasProperty("remoteApi")) {
        if (auto* remoteObj = obj->getProperty("remoteApi").getDynamicObject()) {
            if (remoteObj->hasProperty("enabled"))
                remoteApiEnabled = remoteObj->getProperty("enabled");
            if (remoteObj->hasProperty("port"))
                remoteApiPort = remoteObj->getProperty("port");
            if (remoteObj->hasProperty("mcpPort"))
                remoteApiMcpPort = remoteObj->getProperty("mcpPort");
            remoteApiAllowedOrigins.clear();
            if (const auto* origins = remoteObj->getProperty("allowedOrigins").getArray())
                for (const auto& origin : *origins)
                    remoteApiAllowedOrigins.push_back(origin.toString().toStdString());
            // Passed through untouched; `RemoteClientRegistry` owns the shape
            // and drops anything it does not recognise.
            remoteApiClients = remoteObj->getProperty("clients");
        }
    }

    // OSC — absent means the defaults, which open no socket.
    if (obj->hasProperty("osc")) {
        if (auto* oscObj = obj->getProperty("osc").getDynamicObject()) {
            if (oscObj->hasProperty("enabled"))
                oscEnabled = oscObj->getProperty("enabled");
            if (oscObj->hasProperty("receivePort"))
                oscReceivePort = oscObj->getProperty("receivePort");
            // An empty stored address would bind nothing at all, so it falls
            // back to the default rather than silently disabling the listener.
            if (oscObj->hasProperty("bindAddress")) {
                auto address = oscObj->getProperty("bindAddress").toString();
                if (address.isNotEmpty())
                    oscBindAddress = address.toStdString();
            }
        }
    }
    recentProjects = getStringArray("recentProjects");
    customPluginPaths = getStringArray("customPluginPaths");
    autoEnabledInsertInputs_ = getStringArray("autoEnabledInsertInputs");
    autoEnabledInsertOutputs_ = getStringArray("autoEnabledInsertOutputs");
    totalPluginCount = getInt("totalPluginCount", totalPluginCount);
    scanPluginsOnStartup = getBool("scanPluginsOnStartup", scanPluginsOnStartup);
    loadModelOnStartup = getBool("loadModelOnStartup", loadModelOnStartup);
    stopUpdatesPlayhead = getBool("stopUpdatesPlayhead", stopUpdatesPlayhead);
    followPlayhead = getBool("followPlayhead", followPlayhead);
    chordPreviewOnByDefault = getBool("chordPreviewOnByDefault", chordPreviewOnByDefault);
    autoCrossfadeByDefault = getBool("autoCrossfadeByDefault", autoCrossfadeByDefault);
    clipOverlapPlaysBoth = getBool("clipOverlapPlaysBoth", clipOverlapPlaysBoth);

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

    // Parameter aliases (user-global layer)
    if (obj->hasProperty("paramAliases"))
        paramAliases_ = obj->getProperty("paramAliases");

    // Controller devices
    if (obj->hasProperty("controllers"))
        controllers_ = obj->getProperty("controllers");

    // Lua controller scripts
    if (obj->hasProperty("luaScripts"))
        luaScripts_ = obj->getProperty("luaScripts");
    activeLuaScript_ = getString("activeLuaScript", activeLuaScript_);
    enabledFactoryLuaScripts_.clear();
    if (obj->hasProperty("enabledFactoryLuaScripts")) {
        auto v = obj->getProperty("enabledFactoryLuaScripts");
        if (v.isArray()) {
            for (const auto& item : *v.getArray())
                enabledFactoryLuaScripts_.push_back(item.toString().toStdString());
        }
    }

    // Global bindings
    if (obj->hasProperty("globalBindings"))
        globalBindings_ = obj->getProperty("globalBindings");

    // Keyboard-shortcut overrides (#20) and mouse-gesture overrides (#21)
    if (obj->hasProperty("keyboardBindings"))
        keyboardBindings_ = obj->getProperty("keyboardBindings");
    if (obj->hasProperty("gestureBindings"))
        gestureBindings_ = obj->getProperty("gestureBindings");

    // MIDI Learn settings
    if (obj->hasProperty("midiLearn")) {
        auto mlVar = obj->getProperty("midiLearn");
        if (auto* mlObj = mlVar.getDynamicObject()) {
            auto scopeStr = mlObj->getProperty("defaultScope").toString();
            midiLearnDefaultScope_ = (scopeStr == "global") ? 0 : 1;
        }
    }

    if (obj->hasProperty("analysisDefaults")) {
        auto adVar = obj->getProperty("analysisDefaults");
        if (auto* adObj = adVar.getDynamicObject()) {
            auto oscVar = adObj->getProperty("oscilloscope");
            if (auto* oscObj = oscVar.getDynamicObject()) {
                if (oscObj->hasProperty("timebaseMs"))
                    oscilloscopeDefaults_.timebaseMs =
                        static_cast<float>(static_cast<double>(oscObj->getProperty("timebaseMs")));
            }
            auto specVar = adObj->getProperty("spectrum");
            if (auto* specObj = specVar.getDynamicObject()) {
                if (specObj->hasProperty("fftOrder"))
                    spectrumDefaults_.fftOrder = static_cast<int>(specObj->getProperty("fftOrder"));
                if (specObj->hasProperty("slopeDbPerOct"))
                    spectrumDefaults_.slopeDbPerOct = static_cast<float>(
                        static_cast<double>(specObj->getProperty("slopeDbPerOct")));
                if (specObj->hasProperty("smoothing"))
                    spectrumDefaults_.smoothing =
                        static_cast<float>(static_cast<double>(specObj->getProperty("smoothing")));
            }
        }
    }

    DBG("Config::load - " + configFile.getFullPathName());
}

}  // namespace magda
