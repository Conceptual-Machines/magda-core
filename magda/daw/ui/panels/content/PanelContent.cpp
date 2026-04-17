#include "PanelContent.hpp"

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

}  // namespace magda::daw::ui
