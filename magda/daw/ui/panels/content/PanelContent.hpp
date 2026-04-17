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
    DrumGridClipView,
    AudioClipProperties
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

    /**
     * @brief Called when the parent panel expands from collapsed state
     * Override to grab focus on a primary input field
     */
    virtual void onPanelExpanded() {}

    /**
     * @brief Whether this content wants the parent panel to show a header bar
     */
    virtual bool wantsHeader() const {
        return false;
    }

    /**
     * @brief Reparent header controls into the given header bar component
     * Called when this content becomes active. Content should call
     * headerBar.addAndMakeVisible() for each control it wants in the header.
     */
    virtual void populateHeader(juce::Component& headerBar) {
        juce::ignoreUnused(headerBar);
    }

    /**
     * @brief Remove header controls from the header bar
     * Called when this content is about to be deactivated. Content should
     * reparent controls back to itself via addChildComponent().
     */
    virtual void depopulateHeader(juce::Component& headerBar) {
        juce::ignoreUnused(headerBar);
    }

    /**
     * @brief Layout header controls within the given bounds
     * Called from the parent panel's resized(). Content positions its
     * controls that were added via populateHeader().
     */
    virtual void layoutHeader(juce::Rectangle<int> headerBounds) {
        juce::ignoreUnused(headerBounds);
    }

    /**
     * @brief Get the optimal panel height for the current content/device.
     * @param windowHeight Total window height for ratio-based calculations.
     * @return Preferred height in pixels, or 0 if no preference (use panel default).
     */
    virtual int getOptimalPanelHeight(int windowHeight) const {
        juce::ignoreUnused(windowHeight);
        return 0;
    }

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
        case PanelContentType::AudioClipProperties:
            return "Properties";
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
        case PanelContentType::AudioClipProperties:
            return "Properties";
    }
    return "Unknown";
}

}  // namespace magda::daw::ui
