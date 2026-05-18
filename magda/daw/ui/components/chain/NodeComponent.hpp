#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

// Forward declarations for panel components
class ModsPanelComponent;
class MacroPanelComponent;
class AIPanelComponent;
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
class NodeComponent : public juce::Component,
                      public magda::SelectionManagerListener,
                      public magda::BindingRegistryListener,
                      public magda::ControllerRegistryListener {
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

    // BindingRegistryListener / ControllerRegistryListener — bindings or
    // a controller's enabled state can change which dot lights up on this
    // node's header. Also fires when the focused-macro-owner shifts (e.g.
    // user selects a different rack), since hasResolverBindingForDevice()
    // resolves through the live ChainContext.
    void bindingRegistryChanged(magda::BindingScope) override {
        refreshControllerIndicators();
    }
    void controllerRegistryChanged() override {
        refreshControllerIndicators();
    }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Header accessors
    void setNodeName(const juce::String& name);
    void setNodeNameFont(const juce::Font& font);
    juce::String getNodeName() const;
    void setBypassed(bool bypassed);
    bool isBypassed() const;
    void setFrozen(bool frozen);
    bool isFrozen() const {
        return frozen_;
    }

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
    bool isAIPanelVisible() const {
        return aiPanelVisible_;
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
    std::function<void(bool)> onAIPanelToggled;
    std::function<void()> onLayoutChanged;         // Called when size changes (e.g., panel toggle)
    std::function<void()> onSelected;              // Called when node is clicked/selected
    std::function<void(bool)> onCollapsedChanged;  // Called when collapsed state changes
    std::function<void(float)> onZoomDelta;        // Called for Cmd+scroll zoom (delta amount)

    // Toggle side panel visibility programmatically
    void setModPanelVisible(bool visible);
    void setParamPanelVisible(bool visible);
    void setGainPanelVisible(bool visible);
    void setAIPanelVisible(bool visible);

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

    /**
     * Override to expose a preset menu button. The base class reserves a
     * slot for it between the power button and whatever resizedHeaderExtra
     * positions, locking the right-side icon order to:
     *
     *     [...subclass extras...][preset][power][delete]
     *
     * Subclasses MUST NOT position this button themselves — the base
     * owns its bounds. Returning nullptr (default) means no preset slot.
     *
     * Existed to fix a recurring class of bugs where new buttons added
     * inside resizedHeaderExtra ended up to the right of the preset
     * button, breaking the [preset][power][delete] right-edge contract.
     */
    virtual juce::Component* getHeaderPresetButton() {
        return nullptr;
    }

    /**
     * Override to substitute a different power/bypass button. Defaults to
     * the base's own bypassButton_; subclasses that want a custom-styled
     * button (e.g. DeviceSlotComponent's red/green SvgButton onButton_)
     * return their own and hide bypassButton_ via setBypassButtonVisible.
     *
     * The base reserves the slot to the LEFT of [delete], so the right
     * edge is always [preset][power][delete] regardless of which subclass
     * supplied the buttons.
     */
    virtual juce::Component* getHeaderPowerButton() {
        return bypassButton_.get();
    }

    // Override to provide a name for the collapsed rotated label
    virtual juce::String getCollapsedName() const {
        return nameLabel_.getText();
    }

    // Override to customize side panel content (mods/params are to the left of node)
    virtual void paintModPanel(juce::Graphics& g, juce::Rectangle<int> panelArea);
    virtual void paintExtraLeftPanel(juce::Graphics& g,
                                     juce::Rectangle<int> panelArea);  // Between mods and params
    virtual void paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea);
    virtual void paintGainPanel(juce::Graphics& g,
                                juce::Rectangle<int> panelArea);  // Gain is below content
    virtual void paintExtraRightPanel(juce::Graphics& g,
                                      juce::Rectangle<int> panelArea);  // After macros
    virtual void paintAIPanel(juce::Graphics& g,
                              juce::Rectangle<int> panelArea);  // After mod editor

    // Override to layout custom panel content
    virtual void resizedModPanel(juce::Rectangle<int> panelArea);
    virtual void resizedExtraLeftPanel(juce::Rectangle<int> panelArea);  // Between mods and params
    virtual void resizedParamPanel(juce::Rectangle<int> panelArea);
    virtual void resizedGainPanel(juce::Rectangle<int> panelArea);
    virtual void resizedExtraRightPanel(juce::Rectangle<int> panelArea);  // After macros
    virtual void resizedAIPanel(juce::Rectangle<int> panelArea);          // After mod editor

    // Override to add extra buttons when collapsed (area is below bypass/delete)
    virtual void resizedCollapsed(juce::Rectangle<int>& area);

    // Override to provide custom panel widths
    virtual int getModPanelWidth() const {
        return DEFAULT_PANEL_WIDTH;  // Mod panel uses 2-column layout
    }
    // Extra left panel (between mods and params) - returns modulator editor width when visible
    virtual int getExtraLeftPanelWidth() const;
    virtual int getParamPanelWidth() const {
        return DEFAULT_PANEL_WIDTH;
    }
    virtual int getGainPanelWidth() const {
        return GAIN_PANEL_WIDTH;
    }
    // AI side panel (sound design prompt + output) — wider than the macro
    // panel because it hosts a text input.
    virtual int getAIPanelWidth() const {
        return AI_PANEL_WIDTH;
    }
    // Extra right panel (after macros) - returns macro editor width when visible
    virtual int getExtraRightPanelWidth() const;

    // Override to hide header (return 0)
    virtual int getHeaderHeight() const {
        return HEADER_HEIGHT;
    }

    // Override to reserve space for a meter strip on the right edge (expanded mode)
    virtual int getMeterWidth() const {
        return 0;
    }

    // Override to reserve space for a meter strip when collapsed (right side of strip)
    // Base class removes this from the right of the collapsed strip before placing buttons
    virtual int getCollapsedMeterWidth() const {
        return 0;
    }

    // Control header button visibility (for custom header layouts)
    void setBypassButtonVisible(bool visible);
    void setDeleteButtonVisible(bool visible);

    // Panel visibility state (accessible to subclasses)
    bool modPanelVisible_ = false;
    bool paramPanelVisible_ = false;
    bool gainPanelVisible_ = false;
    bool aiPanelVisible_ = false;

    // Selection state
    bool selected_ = false;
    bool frozen_ = false;
    bool mouseDownForSelection_ = false;

    // Collapsed state (show header only)
    bool collapsed_ = false;
    juce::Rectangle<int> collapsedTextArea_;   // Area for rotated name when collapsed
    juce::Rectangle<int> collapsedMeterArea_;  // Area for meter strip when collapsed

    // Drag-to-reorder state
    bool draggable_ = true;
    bool isDragging_ = false;
    juce::Point<int> dragStartPos_;     // In parent coordinates
    juce::Point<int> dragStartBounds_;  // Component position at drag start
    static constexpr int DRAG_THRESHOLD = 5;

    // Unique path for centralized selection
    magda::ChainNodePath nodePath_;

    // Controller-binding indicator state (drawn as small dots in the header
    // by paintOverChildren). Recomputed by refreshControllerIndicators() in
    // response to BindingRegistry / ControllerRegistry / chain-focus changes.
    //   pinned (orange) — any user-mapped binding (Static or Alias) for this
    //                     node. Survives focus changes.
    //   automap (green) — at least one resolver binding (e.g. focused.macro)
    //                     currently resolves to this node. Tracks focus.
    bool hasPinnedBindings_ = false;
    bool hasAutomapBindings_ = false;
    void refreshControllerIndicators();

    // Anchor for the controller-indicator dot(s). Default reads
    // nameLabel_'s rendered text width; subclasses with a custom logo
    // (Drum Grid's "MDG2000" etc.) override this to place the dot next to
    // their own visible text rather than the (possibly empty) label.
    // Returning a negative x suppresses dot painting.
    virtual juce::Point<float> getControllerIndicatorAnchor() const;

    // Layout constants
    static constexpr int HEADER_HEIGHT = 24;
    static constexpr int BUTTON_SIZE = 18;
    static constexpr int DEFAULT_PANEL_WIDTH = 150;  // Width for 2-column panels (params, macros)
    static constexpr int AI_PANEL_WIDTH = 200;       // Width for AI sound-design panel
    static constexpr int SINGLE_COLUMN_PANEL_WIDTH = 70;  // Width for 1-column panels (mods)
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
    virtual std::map<magda::DeviceId, std::vector<juce::String>> getDeviceParamNames() const {
        return {};
    }

    // Virtual callbacks for mod/macro changes (subclasses implement to persist changes)
    virtual void onModTargetChangedInternal(int /*modIndex*/, magda::ControlTarget /*target*/) {}
    virtual void onModNameChangedInternal(int /*modIndex*/, const juce::String& /*name*/) {}
    virtual void onModTypeChangedInternal(int /*modIndex*/, magda::ModType /*type*/) {}
    virtual void onModWaveformChangedInternal(int /*modIndex*/, magda::LFOWaveform /*waveform*/) {}
    virtual void onModRateChangedInternal(int /*modIndex*/, float /*rate*/) {}
    virtual void onModPhaseOffsetChangedInternal(int /*modIndex*/, float /*phaseOffset*/) {}
    virtual void onModTempoSyncChangedInternal(int /*modIndex*/, bool /*tempoSync*/) {}
    virtual void onModSyncDivisionChangedInternal(int /*modIndex*/,
                                                  magda::SyncDivision /*division*/) {}
    virtual void onModTriggerModeChangedInternal(int /*modIndex*/, magda::LFOTriggerMode /*mode*/) {
    }
    virtual void onModAudioAttackChangedInternal(int /*modIndex*/, float /*ms*/) {}
    virtual void onModAudioReleaseChangedInternal(int /*modIndex*/, float /*ms*/) {}
    virtual void onModCurveChangedInternal(int /*modIndex*/) {}
    // Contextual link callbacks (when param is selected and mod amount slider is used)
    virtual void onModLinkAmountChangedInternal(int /*modIndex*/, magda::ControlTarget /*target*/,
                                                float /*amount*/) {}
    virtual void onModLinkEnabledChangedInternal(int /*modIndex*/, magda::ControlTarget /*target*/,
                                                 bool /*enabled*/) {}
    virtual void onModNewLinkCreatedInternal(int /*modIndex*/, magda::ControlTarget /*target*/,
                                             float /*amount*/) {}
    virtual void onModLinkRemovedInternal(int /*modIndex*/, magda::ControlTarget /*target*/) {}
    virtual void onModAllLinksClearedInternal(int /*modIndex*/) {}
    virtual void onMacroValueChangedInternal(int /*macroIndex*/, float /*value*/) {}
    virtual void onMacroTargetChangedInternal(int /*macroIndex*/, magda::ControlTarget /*target*/) {
    }
    virtual void onMacroNameChangedInternal(int /*macroIndex*/, const juce::String& /*name*/) {}
    virtual void onMacroAllLinksClearedInternal(int /*macroIndex*/) {}
    // Contextual link callbacks for macros (similar to mods)
    virtual void onMacroLinkAmountChangedInternal(int /*macroIndex*/,
                                                  magda::ControlTarget /*target*/,
                                                  float /*amount*/) {}
    virtual void onMacroNewLinkCreatedInternal(int /*macroIndex*/, magda::ControlTarget /*target*/,
                                               float /*amount*/) {}
    virtual void onMacroLinkRemovedInternal(int /*macroIndex*/, magda::ControlTarget /*target*/) {}
    virtual void onMacroLinkBipolarChangedInternal(int /*macroIndex*/,
                                                   magda::ControlTarget /*target*/,
                                                   bool /*bipolar*/) {}
    virtual void onModClickedInternal(int /*modIndex*/) {}
    virtual void onMacroClickedInternal(int /*macroIndex*/) {}
    virtual void onAddModRequestedInternal(int /*slotIndex*/, magda::ModType /*type*/,
                                           magda::LFOWaveform /*waveform*/) {}
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
    std::unique_ptr<AIPanelComponent> aiPanel_;
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

  public:
    // Public so external TrackManagerListener callbacks (e.g. controller writes)
    // can force a redraw when the macro value was mutated externally — the knob
    // slider only auto-updates on local mouse interaction.
    void updateMacroPanel();

    /// Refresh both the macro and mods panels (each guarded by its own
    /// visibility flag). Use this after any operation that affects both
    /// panels' contents — e.g. adding/removing a mod can invalidate the
    /// macro-link menu's mod-target list. Centralises what the pre-step-4
    /// call sites did inline.
    void refreshPanels();

    // Lightweight variant for high-rate external writes (e.g. controller
    // automap) — updates just one knob's displayed value without rebuilding
    // the panel, available-devices list, or param-name map.
    void updateMacroValueDisplay(int macroIndex, float value);

  private:
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

  protected:
    // Accessors so subclasses (e.g. DeviceSlotComponent) can paint badges
    // positioned relative to header controls without accessing the raw members.
    const juce::Label& getNameLabel() const {
        return nameLabel_;
    }

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
