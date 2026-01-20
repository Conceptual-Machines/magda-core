#pragma once

#include <vector>

#include "../content/PanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Identifies which panel location we're referring to
 */
enum class PanelLocation { Left, Right, Bottom };

/**
 * @brief State for a single panel
 */
struct PanelState {
    PanelLocation location;
    std::vector<PanelContentType> tabs;  // Ordered list of tabs (max 4)
    int activeTabIndex = 0;              // Index of currently active tab
    bool collapsed = false;
    int size = 0;  // Width for left/right, height for bottom (0 = default)

    /**
     * @brief Get the currently active content type
     */
    PanelContentType getActiveContentType() const {
        if (tabs.empty() || activeTabIndex < 0 || activeTabIndex >= static_cast<int>(tabs.size())) {
            return PanelContentType::PluginBrowser;  // Fallback
        }
        return tabs[static_cast<size_t>(activeTabIndex)];
    }

    /**
     * @brief Check if this panel has a specific content type
     */
    bool hasContentType(PanelContentType type) const {
        for (const auto& tab : tabs) {
            if (tab == type)
                return true;
        }
        return false;
    }

    /**
     * @brief Get index of a content type, or -1 if not found
     */
    int getTabIndex(PanelContentType type) const {
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (tabs[i] == type)
                return static_cast<int>(i);
        }
        return -1;
    }
};

/**
 * @brief Complete state for all panels
 */
struct AllPanelStates {
    PanelState leftPanel;
    PanelState rightPanel;
    PanelState bottomPanel;

    /**
     * @brief Get panel state by location
     */
    const PanelState& getPanel(PanelLocation location) const {
        switch (location) {
            case PanelLocation::Left:
                return leftPanel;
            case PanelLocation::Right:
                return rightPanel;
            case PanelLocation::Bottom:
                return bottomPanel;
        }
        return leftPanel;  // Fallback
    }

    /**
     * @brief Get mutable panel state by location
     */
    PanelState& getPanel(PanelLocation location) {
        switch (location) {
            case PanelLocation::Left:
                return leftPanel;
            case PanelLocation::Right:
                return rightPanel;
            case PanelLocation::Bottom:
                return bottomPanel;
        }
        return leftPanel;  // Fallback
    }
};

/**
 * @brief Default panel configuration
 */
inline AllPanelStates getDefaultPanelStates() {
    AllPanelStates states;

    // Left Panel: Plugin, Sample, Preset browsers
    states.leftPanel.location = PanelLocation::Left;
    states.leftPanel.tabs = {PanelContentType::PluginBrowser, PanelContentType::SampleBrowser,
                             PanelContentType::PresetBrowser};
    states.leftPanel.activeTabIndex = 0;
    states.leftPanel.collapsed = false;

    // Right Panel: Inspector, AI Chat
    states.rightPanel.location = PanelLocation::Right;
    states.rightPanel.tabs = {PanelContentType::Inspector, PanelContentType::AIChatConsole};
    states.rightPanel.activeTabIndex = 0;
    states.rightPanel.collapsed = false;

    // Bottom Panel: Piano Roll, Waveform Editor, Track Chain, Scripting Console
    states.bottomPanel.location = PanelLocation::Bottom;
    states.bottomPanel.tabs = {PanelContentType::PianoRoll, PanelContentType::WaveformEditor,
                               PanelContentType::TrackChain, PanelContentType::ScriptingConsole};
    states.bottomPanel.activeTabIndex = 0;
    states.bottomPanel.collapsed = false;

    return states;
}

}  // namespace magda::daw::ui
