#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/SelectionManager.hpp"
#include "ui/components/common/SvgButton.hpp"

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
 * │ [Mods Panel]  [Content]  [Gain Panel]                   │ ← Side panels (optional)
 * └─────────────────────────────────────────────────────────┘
 */
class NodeComponent : public juce::Component, public magda::SelectionManagerListener {
  public:
    NodeComponent();
    ~NodeComponent() override;

    // Set the unique path for this node (required for centralized selection)
    void setNodePath(const magda::ChainNodePath& path);
    const magda::ChainNodePath& getNodePath() const {
        return nodePath_;
    }

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;
    void chainNodeReselected(const magda::ChainNodePath& path) override;

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

    // Selection
    void setSelected(bool selected);
    bool isSelected() const {
        return selected_;
    }

    // Collapse (show header only)
    void setCollapsed(bool collapsed);
    bool isCollapsed() const {
        return collapsed_;
    }

    // Callbacks
    std::function<void(bool)> onBypassChanged;
    std::function<void()> onDeleteClicked;
    std::function<void(bool)> onModPanelToggled;
    std::function<void(bool)> onParamPanelToggled;
    std::function<void(bool)> onGainPanelToggled;
    std::function<void()> onLayoutChanged;         // Called when size changes (e.g., panel toggle)
    std::function<void()> onSelected;              // Called when node is clicked/selected
    std::function<void(bool)> onCollapsedChanged;  // Called when collapsed state changes

    // Toggle side panel visibility programmatically
    void setModPanelVisible(bool visible);
    void setParamPanelVisible(bool visible);
    void setGainPanelVisible(bool visible);

    // Drag-to-reorder callbacks (for parent container coordination)
    std::function<void(NodeComponent*, const juce::MouseEvent&)> onDragStart;
    std::function<void(NodeComponent*, const juce::MouseEvent&)> onDragMove;
    std::function<void(NodeComponent*, const juce::MouseEvent&)> onDragEnd;

    // Mouse handling for selection and drag-to-reorder
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Get total width of left side panels (mods + params)
    int getLeftPanelsWidth() const;
    // Get total width of right side panels (gain)
    int getRightPanelsWidth() const;
    // Get total preferred width given a base content width
    int getTotalWidth(int baseContentWidth) const;

    // Virtual method for subclasses to report their preferred width
    virtual int getPreferredWidth() const {
        if (collapsed_) {
            // When collapsed, still add side panel widths
            return getLeftPanelsWidth() + COLLAPSED_WIDTH + getRightPanelsWidth();
        }
        return getTotalWidth(200);  // Default base width
    }

    // Width when collapsed
    static constexpr int COLLAPSED_WIDTH = 40;

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

    // Override to add extra buttons when collapsed (area is below bypass/delete)
    virtual void resizedCollapsed(juce::Rectangle<int>& area);

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

    // Control header button visibility (for custom header layouts)
    void setBypassButtonVisible(bool visible);
    void setDeleteButtonVisible(bool visible);

    // Panel visibility state (accessible to subclasses)
    bool modPanelVisible_ = false;
    bool paramPanelVisible_ = false;
    bool gainPanelVisible_ = false;

    // Selection state
    bool selected_ = false;
    bool mouseDownForSelection_ = false;

    // Collapsed state (show header only)
    bool collapsed_ = false;

    // Drag-to-reorder state
    bool draggable_ = true;
    bool isDragging_ = false;
    juce::Point<int> dragStartPos_;     // In parent coordinates
    juce::Point<int> dragStartBounds_;  // Component position at drag start
    static constexpr int DRAG_THRESHOLD = 5;

    // Unique path for centralized selection
    magda::ChainNodePath nodePath_;

    // Layout constants
    static constexpr int HEADER_HEIGHT = 20;
    static constexpr int BUTTON_SIZE = 16;
    static constexpr int DEFAULT_PANEL_WIDTH = 60;  // Width for side panels (mods, params)
    static constexpr int GAIN_PANEL_WIDTH = 32;     // Width for gain panel (right side)

  private:
    // Header controls
    std::unique_ptr<magda::SvgButton> bypassButton_;
    juce::Label nameLabel_;
    juce::TextButton deleteButton_;

    // Mod panel controls (3 modulator slots)
    std::unique_ptr<juce::TextButton> modSlotButtons_[3];

    // Param panel controls (4 knobs in 2x2 grid)
    std::vector<std::unique_ptr<juce::Slider>> paramKnobs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeComponent)
};

}  // namespace magda::daw::ui
