#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace magda::daw::ui {

/**
 * @brief Base class for chain nodes (Device, Rack, Chain)
 *
 * Provides common layout structure:
 * ┌─────────────────────────────────────────────────────────┐
 * │ [B] Name                                           [X]  │ ← Header
 * ├─────────────────────────────────────────────────────────┤
 * │                    Content Area                         │ ← Content (subclass)
 * ├─────────────────────────────────────────────────────────┤
 * │ Modulators Panel (when M toggled)                       │ ← Optional
 * ├─────────────────────────────────────────────────────────┤
 * │ Parameters Panel (when P toggled)                       │ ← Optional
 * ├─────────────────────────────────────────────────────────┤
 * │ Gain Panel (when G toggled)                             │ ← Optional
 * ├─────────────────────────────────────────────────────────┤
 * │ [M] [P]                                            [G]  │ ← Footer
 * └─────────────────────────────────────────────────────────┘
 */
class NodeComponent : public juce::Component {
  public:
    NodeComponent();
    ~NodeComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Header accessors
    void setNodeName(const juce::String& name);
    juce::String getNodeName() const;
    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // Panel visibility
    bool isModPanelVisible() const {
        return modPanelVisible_;
    }
    bool isParamPanelVisible() const {
        return paramPanelVisible_;
    }
    bool isGainPanelVisible() const {
        return gainPanelVisible_;
    }

    // Callbacks
    std::function<void(bool)> onBypassChanged;
    std::function<void()> onDeleteClicked;
    std::function<void(bool)> onModPanelToggled;
    std::function<void(bool)> onParamPanelToggled;
    std::function<void(bool)> onGainPanelToggled;
    std::function<void()> onLayoutChanged;  // Called when size changes (e.g., panel toggle)

    // Get total width of left side panels (mods + params)
    int getLeftPanelsWidth() const;
    // Get total width of right side panels (gain)
    int getRightPanelsWidth() const;
    // Get total preferred width given a base content width
    int getTotalWidth(int baseContentWidth) const;

  protected:
    // Override these to customize content
    virtual void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea);
    virtual void resizedContent(juce::Rectangle<int> contentArea);

    // Override to add extra header buttons (between name and delete)
    virtual void resizedHeaderExtra(juce::Rectangle<int>& headerArea);

    // Override to customize side panel content (mods/params are to the left of node)
    virtual void paintModPanel(juce::Graphics& g, juce::Rectangle<int> panelArea);
    virtual void paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea);
    virtual void paintGainPanel(juce::Graphics& g,
                                juce::Rectangle<int> panelArea);  // Gain is below content

    // Override to layout custom panel content
    virtual void resizedModPanel(juce::Rectangle<int> panelArea);
    virtual void resizedParamPanel(juce::Rectangle<int> panelArea);
    virtual void resizedGainPanel(juce::Rectangle<int> panelArea);

    // Override to provide custom panel widths
    virtual int getModPanelWidth() const {
        return DEFAULT_PANEL_WIDTH;
    }
    virtual int getParamPanelWidth() const {
        return DEFAULT_PANEL_WIDTH;
    }
    virtual int getGainPanelWidth() const {
        return GAIN_PANEL_WIDTH;
    }

    // Override to hide header (return 0)
    virtual int getHeaderHeight() const {
        return HEADER_HEIGHT;
    }

    // Control footer button visibility
    void setParamButtonVisible(bool visible);
    void setModButtonVisible(bool visible);
    void setGainButtonVisible(bool visible);

    // Panel visibility state (accessible to subclasses)
    bool modPanelVisible_ = false;
    bool paramPanelVisible_ = false;
    bool gainPanelVisible_ = false;

    // Layout constants
    static constexpr int HEADER_HEIGHT = 20;
    static constexpr int FOOTER_HEIGHT = 20;
    static constexpr int BUTTON_SIZE = 16;
    static constexpr int DEFAULT_PANEL_WIDTH = 60;  // Width for side panels (mods, params)
    static constexpr int GAIN_PANEL_WIDTH = 32;     // Width for gain panel (right side)

  private:
    // Header controls
    juce::TextButton bypassButton_;
    juce::Label nameLabel_;
    juce::TextButton deleteButton_;

    // Footer controls (panel toggles)
    juce::TextButton modToggleButton_;
    juce::TextButton paramToggleButton_;
    juce::TextButton gainToggleButton_;

    // Mod panel controls (3 modulator slots)
    std::unique_ptr<juce::TextButton> modSlotButtons_[3];

    // Param panel controls (4 knobs in 2x2 grid)
    std::vector<std::unique_ptr<juce::Slider>> paramKnobs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeComponent)
};

}  // namespace magda::daw::ui
