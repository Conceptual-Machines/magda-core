#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

// Forward declarations for panel components
class ModsPanelComponent;
class MacroPanelComponent;
class ModulatorEditorPanel;
class MacroEditorPanel;

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
    virtual void setNodePath(const magda::ChainNodePath& path);
    const magda::ChainNodePath& getNodePath() const {
        return nodePath_;
    }

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;
    void chainNodeReselected(const magda::ChainNodePath& path) override;
    void paramSelectionChanged(const magda::ParamSelection& selection) override;

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
    std::function<void(float)> onZoomDelta;        // Called for Cmd+scroll zoom (delta amount)

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
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Get total width of left side panels (mods + params + any extras)
    virtual int getLeftPanelsWidth() const;
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
    virtual void paintExtraLeftPanel(juce::Graphics& g,
                                     juce::Rectangle<int> panelArea);  // Between mods and params
    virtual void paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea);
    virtual void paintGainPanel(juce::Graphics& g,
                                juce::Rectangle<int> panelArea);  // Gain is below content
    virtual void paintExtraRightPanel(juce::Graphics& g,
                                      juce::Rectangle<int> panelArea);  // After macros

    // Override to layout custom panel content
    virtual void resizedModPanel(juce::Rectangle<int> panelArea);
    virtual void resizedExtraLeftPanel(juce::Rectangle<int> panelArea);  // Between mods and params
    virtual void resizedParamPanel(juce::Rectangle<int> panelArea);
    virtual void resizedGainPanel(juce::Rectangle<int> panelArea);
    virtual void resizedExtraRightPanel(juce::Rectangle<int> panelArea);  // After macros

    // Override to add extra buttons when collapsed (area is below bypass/delete)
    virtual void resizedCollapsed(juce::Rectangle<int>& area);

    // Override to provide custom panel widths
    virtual int getModPanelWidth() const {
        return SINGLE_COLUMN_PANEL_WIDTH;  // Mod panel uses single column
    }
    // Extra left panel (between mods and params) - returns modulator editor width when visible
    virtual int getExtraLeftPanelWidth() const;
    virtual int getParamPanelWidth() const {
        return DEFAULT_PANEL_WIDTH;
    }
    virtual int getGainPanelWidth() const {
        return GAIN_PANEL_WIDTH;
    }
    // Extra right panel (after macros) - returns macro editor width when visible
    virtual int getExtraRightPanelWidth() const;

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
    static constexpr int DEFAULT_PANEL_WIDTH = 100;  // Width for 2-column panels (params, macros)
    static constexpr int SINGLE_COLUMN_PANEL_WIDTH = 55;  // Width for 1-column panels (mods)
    static constexpr int GAIN_PANEL_WIDTH = 32;           // Width for gain panel (right side)

    // === Mods/Macros Panel Support ===

    // Virtual methods for subclasses to provide mod/macro data
    // Return nullptr if this node type doesn't have mods/macros
    virtual const magda::ModArray* getModsData() const {
        return nullptr;
    }
    virtual const magda::MacroArray* getMacrosData() const {
        return nullptr;
    }

    // Virtual methods for subclasses to provide available link targets
    virtual std::vector<std::pair<magda::DeviceId, juce::String>> getAvailableDevices() const {
        return {};
    }

    // Virtual callbacks for mod/macro changes (subclasses implement to persist changes)
    virtual void onModAmountChangedInternal(int /*modIndex*/, float /*amount*/) {}
    virtual void onModTargetChangedInternal(int /*modIndex*/, magda::ModTarget /*target*/) {}
    virtual void onModNameChangedInternal(int /*modIndex*/, const juce::String& /*name*/) {}
    virtual void onModTypeChangedInternal(int /*modIndex*/, magda::ModType /*type*/) {}
    virtual void onModRateChangedInternal(int /*modIndex*/, float /*rate*/) {}
    virtual void onModWaveformChangedInternal(int /*modIndex*/, magda::LFOWaveform /*waveform*/) {}
    // Contextual link callbacks (when param is selected and mod amount slider is used)
    virtual void onModLinkAmountChangedInternal(int /*modIndex*/, magda::ModTarget /*target*/,
                                                float /*amount*/) {}
    virtual void onModNewLinkCreatedInternal(int /*modIndex*/, magda::ModTarget /*target*/,
                                             float /*amount*/) {}
    virtual void onModLinkRemovedInternal(int /*modIndex*/, magda::ModTarget /*target*/) {}
    virtual void onMacroValueChangedInternal(int /*macroIndex*/, float /*value*/) {}
    virtual void onMacroTargetChangedInternal(int /*macroIndex*/, magda::MacroTarget /*target*/) {}
    virtual void onMacroNameChangedInternal(int /*macroIndex*/, const juce::String& /*name*/) {}
    // Contextual link callbacks for macros (similar to mods)
    virtual void onMacroLinkAmountChangedInternal(int /*macroIndex*/, magda::MacroTarget /*target*/,
                                                  float /*amount*/) {}
    virtual void onMacroNewLinkCreatedInternal(int /*macroIndex*/, magda::MacroTarget /*target*/,
                                               float /*amount*/) {}
    virtual void onMacroLinkRemovedInternal(int /*macroIndex*/, magda::MacroTarget /*target*/) {}
    virtual void onModClickedInternal(int /*modIndex*/) {}
    virtual void onMacroClickedInternal(int /*macroIndex*/) {}
    virtual void onAddModRequestedInternal(int /*slotIndex*/, magda::ModType /*type*/) {}
    virtual void onModRemoveRequestedInternal(int /*modIndex*/) {}
    virtual void onModEnableToggledInternal(int /*modIndex*/, bool /*enabled*/) {}

    // Virtual callbacks for page management (subclasses implement to persist)
    virtual void onModPageAddRequested(int /*itemsToAdd*/) {}
    virtual void onModPageRemoveRequested(int /*itemsToRemove*/) {}
    virtual void onMacroPageAddRequested(int /*itemsToAdd*/) {}
    virtual void onMacroPageRemoveRequested(int /*itemsToRemove*/) {}

    // Panel components (created by NodeComponent, populated by subclass data)
    std::unique_ptr<ModsPanelComponent> modsPanel_;
    std::unique_ptr<MacroPanelComponent> macroPanel_;
    std::unique_ptr<ModulatorEditorPanel> modulatorEditorPanel_;
    std::unique_ptr<MacroEditorPanel> macroEditorPanel_;

    // Editor panel state
    bool modulatorEditorVisible_ = false;
    bool macroEditorVisible_ = false;
    int selectedModIndex_ = -1;
    int selectedMacroIndex_ = -1;

    // Mod/Macro panel management
    void initializeModsMacrosPanels();
    void updateModsPanel();
    void updateMacroPanel();

    // Editor panel management
    void showModulatorEditor(int modIndex);
    void hideModulatorEditor();
    void updateModulatorEditor();
    void showMacroEditor(int macroIndex);
    void hideMacroEditor();
    void updateMacroEditor();

    // Width calculations for editor panels
    int getModulatorEditorWidth() const;
    int getMacroEditorWidth() const;

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
