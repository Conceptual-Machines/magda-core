#include "Config.hpp"

#include <juce_core/juce_core.h>

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

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void Config::save() {
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());

    // Timeline
    root->setProperty("defaultTimelineLength", defaultTimelineLength);
    root->setProperty("defaultZoomViewDuration", defaultZoomViewDuration);

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

    // Render
    root->setProperty("renderFolder", toJuceString(renderFolder));

    // Audio devices
    root->setProperty("preferredAudioDevice", toJuceString(preferredAudioDevice));
    root->setProperty("preferredInputDevice", toJuceString(preferredInputDevice));
    root->setProperty("preferredOutputDevice", toJuceString(preferredOutputDevice));
    root->setProperty("preferredInputChannels", preferredInputChannels);
    root->setProperty("preferredOutputChannels", preferredOutputChannels);

    // AI
    root->setProperty("openaiApiKey", toJuceString(openaiApiKey));
    root->setProperty("openaiModel", toJuceString(openaiModel));

    // Browser
    root->setProperty("browserDefaultDirectory", toJuceString(browserDefaultDirectory));

    juce::Array<juce::var> favArray;
    for (const auto& f : browserFavorites)
        favArray.add(toJuceString(f));
    root->setProperty("browserFavorites", favArray);

    // Custom plugin paths
    juce::Array<juce::var> pluginPathArray;
    for (const auto& p : customPluginPaths)
        pluginPathArray.add(toJuceString(p));
    root->setProperty("customPluginPaths", pluginPathArray);

    // Write to disk
    auto configFile = getConfigFile();
    configFile.getParentDirectory().createDirectory();

    auto json = juce::JSON::toString(juce::var(root.get()));
    if (!configFile.replaceWithText(json))
        DBG("Config::save - failed to write " + configFile.getFullPathName());
    else
        DBG("Config::save - " + configFile.getFullPathName());
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

    defaultTimelineLength = getDouble("defaultTimelineLength", defaultTimelineLength);
    defaultZoomViewDuration = getDouble("defaultZoomViewDuration", defaultZoomViewDuration);
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

    renderFolder = getString("renderFolder", renderFolder);

    preferredAudioDevice = getString("preferredAudioDevice", preferredAudioDevice);
    preferredInputDevice = getString("preferredInputDevice", preferredInputDevice);
    preferredOutputDevice = getString("preferredOutputDevice", preferredOutputDevice);
    preferredInputChannels = getInt("preferredInputChannels", preferredInputChannels);
    preferredOutputChannels = getInt("preferredOutputChannels", preferredOutputChannels);

    openaiApiKey = getString("openaiApiKey", openaiApiKey);
    openaiModel = getString("openaiModel", openaiModel);

    browserDefaultDirectory = getString("browserDefaultDirectory", browserDefaultDirectory);
    browserFavorites = getStringArray("browserFavorites");
    customPluginPaths = getStringArray("customPluginPaths");

    DBG("Config::load - " + configFile.getFullPathName());
}

}  // namespace magda
