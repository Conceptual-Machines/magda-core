#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief Enum defining all available panel content types
 */
enum class PanelContentType {
    Empty,  // No selection - shows nothing
    PluginBrowser,
    MediaExplorer,
    PresetBrowser,
    Inspector,
    AIChatConsole,
    ScriptingConsole,
    TrackChain,
    PianoRoll,
    WaveformEditor,
    DrumGridClipView
};

/**
 * @brief Information about a panel content type
 */
struct PanelContentInfo {
    PanelContentType type;
    juce::String name;
    juce::String description;
    juce::String iconName;  // Name of SVG icon in BinaryData
};

/**
 * @brief Abstract base class for all panel content types
 *
 * Each content type (browser, inspector, console, etc.) inherits from this
 * and provides its own UI implementation. Content instances are created
 * lazily and cached by TabbedPanel.
 */
class PanelContent : public juce::Component {
  public:
    PanelContent() = default;
    ~PanelContent() override = default;

    /**
     * @brief Get the content type identifier
     */
    virtual PanelContentType getContentType() const = 0;

    /**
     * @brief Get metadata about this content type
     */
    virtual PanelContentInfo getContentInfo() const = 0;

    /**
     * @brief Called when this content becomes the active tab
     * Override to refresh data or start updates
     */
    virtual void onActivated() {}

    /**
     * @brief Called when this content is no longer the active tab
     * Override to pause updates or save state
     */
    virtual void onDeactivated() {}

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelContent)
};

/**
 * @brief Helper function to get display name for a content type
 */
inline juce::String getContentTypeName(PanelContentType type) {
    switch (type) {
        case PanelContentType::Empty:
            return "";
        case PanelContentType::PluginBrowser:
            return "Plugins";
        case PanelContentType::MediaExplorer:
            return "Samples";
        case PanelContentType::PresetBrowser:
            return "Presets";
        case PanelContentType::Inspector:
            return "Inspector";
        case PanelContentType::AIChatConsole:
            return "AI Chat";
        case PanelContentType::ScriptingConsole:
            return "Script";
        case PanelContentType::TrackChain:
            return "Track Chain";
        case PanelContentType::PianoRoll:
            return "Piano Roll";
        case PanelContentType::WaveformEditor:
            return "Waveform";
        case PanelContentType::DrumGridClipView:
            return "Drum Grid";
    }
    return "Unknown";
}

/**
 * @brief Helper function to get icon name for a content type
 */
inline juce::String getContentTypeIcon(PanelContentType type) {
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
    }
    return "Unknown";
}

}  // namespace magda::daw::ui
