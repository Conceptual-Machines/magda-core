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

// Ids are written to config.json, so they are frozen once shipped: renaming a
// PanelContentType must not change the id it maps to here.
//
// Deliberately a switch with no default, so adding a content type without
// giving it an id is a -Wswitch warning rather than a silent persistence hole.
// An unmapped type would serialise as an empty id, which reads back as "tab
// this build does not know" and quietly sorts to the end of the saved order.
juce::String getContentTypeId(PanelContentType type) {
    switch (type) {
        case PanelContentType::Empty:
            return "empty";
        case PanelContentType::PluginBrowser:
            return "pluginBrowser";
        case PanelContentType::MediaExplorer:
            return "mediaExplorer";
        case PanelContentType::PresetBrowser:
            return "presetBrowser";
        case PanelContentType::Inspector:
            return "inspector";
        case PanelContentType::AIChatConsole:
            return "aiChatConsole";
        case PanelContentType::ScriptingConsole:
            return "scriptingConsole";
        case PanelContentType::TrackChain:
            return "trackChain";
        case PanelContentType::PianoRoll:
            return "pianoRoll";
        case PanelContentType::WaveformEditor:
            return "waveformEditor";
        case PanelContentType::DrumGridClipView:
            return "drumGridClipView";
        case PanelContentType::ChordClipView:
            return "chordClipView";
        case PanelContentType::AutomationClipEditor:
            return "automationClipEditor";
        case PanelContentType::AudioClipProperties:
            return "audioClipProperties";
    }
    return {};
}

std::optional<PanelContentType> contentTypeFromId(const juce::String& id) {
    if (id.isEmpty())
        return std::nullopt;

    // Reverse lookup runs through getContentTypeId, so the id strings have
    // exactly one definition and the two directions cannot disagree. The list
    // of types below is the one thing a new content type has to be added to
    // here; forgetting it costs that tab its saved position (it reverts to the
    // default one) rather than corrupting anything. test_panel_tab_persistence
    // round-trips every entry.
    for (auto type : allContentTypes()) {
        if (id == getContentTypeId(type))
            return type;
    }
    return std::nullopt;
}

std::array<PanelContentType, 14> allContentTypes() {
    return {PanelContentType::Empty,
            PanelContentType::PluginBrowser,
            PanelContentType::MediaExplorer,
            PanelContentType::PresetBrowser,
            PanelContentType::Inspector,
            PanelContentType::AIChatConsole,
            PanelContentType::ScriptingConsole,
            PanelContentType::TrackChain,
            PanelContentType::PianoRoll,
            PanelContentType::WaveformEditor,
            PanelContentType::DrumGridClipView,
            PanelContentType::ChordClipView,
            PanelContentType::AutomationClipEditor,
            PanelContentType::AudioClipProperties};
}

}  // namespace magda::daw::ui
