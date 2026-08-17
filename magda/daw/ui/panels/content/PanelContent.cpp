#include "PanelContent.hpp"

#include <array>

#include "core/StringTable.hpp"

namespace magda::daw::ui {

juce::String getContentTypeName(PanelContentType type) {
    switch (type) {
        case PanelContentType::Empty:
            return "";
        case PanelContentType::PluginBrowser:
            return tr("panels.plugins");
        case PanelContentType::MediaExplorer:
            return tr("panels.samples");
        case PanelContentType::PresetBrowser:
            return tr("panels.presets");
        case PanelContentType::Inspector:
            return tr("panels.inspector");
        case PanelContentType::AIChatConsole:
            return tr("panels.ai_chat");
        case PanelContentType::ScriptingConsole:
            return tr("panels.script");
        case PanelContentType::TrackChain:
            return tr("panels.track_chain");
        case PanelContentType::PianoRoll:
            return tr("panels.piano_roll");
        case PanelContentType::WaveformEditor:
            return tr("panels.waveform");
        case PanelContentType::DrumGridClipView:
            return tr("panels.drum_grid");
        case PanelContentType::AudioClipProperties:
            return tr("panels.properties");
    }
    return "Unknown";
}

juce::String getContentTypeIcon(PanelContentType type) {
    switch (type) {
        case PanelContentType::Empty:
            return "";
        case PanelContentType::PluginBrowser:
            return "Plugin";
        case PanelContentType::MediaExplorer:
            return "Sample";
        case PanelContentType::PresetBrowser:
            return "Preset";
        case PanelContentType::Inspector:
            return "Inspector";
        case PanelContentType::AIChatConsole:
            return "AIChat";
        case PanelContentType::ScriptingConsole:
            return "Script";
        case PanelContentType::TrackChain:
            return "Chain";
        case PanelContentType::PianoRoll:
            return "PianoRoll";
        case PanelContentType::WaveformEditor:
            return "Waveform";
        case PanelContentType::DrumGridClipView:
            return "DrumGrid";
        case PanelContentType::AudioClipProperties:
            return "Properties";
    }
    return "Unknown";
}

namespace {
// One row per content type. Ids are written to config.json, so they are frozen
// once shipped: rename a PanelContentType and the id here has to stay put.
struct ContentTypeIdEntry {
    PanelContentType type;
    const char* id;
};

constexpr std::array<ContentTypeIdEntry, 14> kContentTypeIds{{
    {PanelContentType::Empty, "empty"},
    {PanelContentType::PluginBrowser, "pluginBrowser"},
    {PanelContentType::MediaExplorer, "mediaExplorer"},
    {PanelContentType::PresetBrowser, "presetBrowser"},
    {PanelContentType::Inspector, "inspector"},
    {PanelContentType::AIChatConsole, "aiChatConsole"},
    {PanelContentType::ScriptingConsole, "scriptingConsole"},
    {PanelContentType::TrackChain, "trackChain"},
    {PanelContentType::PianoRoll, "pianoRoll"},
    {PanelContentType::WaveformEditor, "waveformEditor"},
    {PanelContentType::DrumGridClipView, "drumGridClipView"},
    {PanelContentType::ChordClipView, "chordClipView"},
    {PanelContentType::AutomationClipEditor, "automationClipEditor"},
    {PanelContentType::AudioClipProperties, "audioClipProperties"},
}};
}  // namespace

juce::String getContentTypeId(PanelContentType type) {
    for (const auto& entry : kContentTypeIds) {
        if (entry.type == type)
            return entry.id;
    }
    return {};
}

std::optional<PanelContentType> contentTypeFromId(const juce::String& id) {
    for (const auto& entry : kContentTypeIds) {
        if (id == entry.id)
            return entry.type;
    }
    return std::nullopt;
}

}  // namespace magda::daw::ui
