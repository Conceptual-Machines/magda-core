#include "ChainPanel.hpp"

#include <BinaryData.h>

#include "NodeComponent.hpp"
#include "RackComponent.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

//==============================================================================
// DeviceSlotComponent - Device display within chain panel (inherits from NodeComponent)
//==============================================================================
class ChainPanel::DeviceSlotComponent : public NodeComponent {
  public:
    static constexpr int BASE_SLOT_WIDTH = 200;  // Base width without panels
    static constexpr int NUM_PARAMS = 16;
    static constexpr int PARAMS_PER_ROW = 4;

    DeviceSlotComponent(ChainPanel& owner, const magda::DeviceInfo& device)
        : owner_(owner), device_(device), gainSlider_(TextSlider::Format::Decibels) {
        setNodeName(device.name);
        setBypassed(device.bypassed);

        // Hide built-in bypass button - we'll add our own in the header
        setBypassButtonVisible(false);

        // Set up callbacks - use path-based methods for proper nesting support
        onDeleteClicked = [this]() {
            magda::TrackManager::getInstance().removeDeviceFromChainByPath(nodePath_);
        };

        // Mod panel toggle updates layout
        onModPanelToggled = [this](bool /*visible*/) { owner_.onDeviceLayoutChanged(); };

        onLayoutChanged = [this]() {
            // Notify ChainPanel to recalculate container size
            owner_.onDeviceLayoutChanged();
        };

        // Hide param button - params shown inline instead
        setParamButtonVisible(false);

        // Mod button (toggle mod panel) - sine wave icon
        modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::sinewavebright_svg,
                                                        BinaryData::sinewavebright_svgSize);
        modButton_->setClickingTogglesState(true);
        modButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
        modButton_->setActiveColor(juce::Colours::white);
        modButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        modButton_->onClick = [this]() {
            modButton_->setActive(modButton_->getToggleState());
            modPanelVisible_ = modButton_->getToggleState();
            if (onModPanelToggled)
                onModPanelToggled(modPanelVisible_);
        };
        addAndMakeVisible(*modButton_);

        // Note: No macro button on devices - params are shown inline

        // Gain text slider in header
        gainSlider_.setRange(-60.0, 12.0, 0.1);
        gainSlider_.setValue(device_.gainDb, juce::dontSendNotification);
        gainSlider_.onValueChanged = [this](double value) {
            // Use path-based lookup for proper nesting support
            if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
                dev->gainDb = static_cast<float>(value);
            }
        };
        addAndMakeVisible(gainSlider_);

        // UI button (open plugin window) - open in new icon
        uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                       BinaryData::open_in_new_svgSize);
        uiButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
        uiButton_->onClick = [this]() { DBG("Open plugin UI for: " << device_.name); };
        addAndMakeVisible(*uiButton_);

        // Bypass/On button (power icon)
        onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                       BinaryData::power_on_svgSize);
        onButton_->setClickingTogglesState(true);
        onButton_->setToggleState(!device.bypassed,
                                  juce::dontSendNotification);  // On = not bypassed
        onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        onButton_->setActiveColor(juce::Colours::white);
        onButton_->setActiveBackgroundColor(
            DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
        onButton_->setActive(!device.bypassed);
        onButton_->onClick = [this]() {
            bool active = onButton_->getToggleState();
            onButton_->setActive(active);
            setBypassed(!active);  // Active = not bypassed
            // Use path-based method for proper nesting support
            magda::TrackManager::getInstance().setDeviceInChainBypassedByPath(nodePath_, !active);
        };
        addAndMakeVisible(*onButton_);

        // Create inline param sliders with labels (mock params)
        // clang-format off
        static const char* mockParamNames[NUM_PARAMS] = {
            "Cutoff", "Resonance", "Drive", "Mix",
            "Attack", "Decay", "Sustain", "Release",
            "LFO Rate", "LFO Depth", "Feedback", "Width",
            "Low", "Mid", "High", "Output"
        };
        // clang-format on

        for (int i = 0; i < NUM_PARAMS; ++i) {
            paramLabels_[i] = std::make_unique<juce::Label>();
            paramLabels_[i]->setText(mockParamNames[i], juce::dontSendNotification);
            paramLabels_[i]->setFont(FontManager::getInstance().getUIFont(9.0f));
            paramLabels_[i]->setColour(juce::Label::textColourId,
                                       DarkTheme::getSecondaryTextColour());
            paramLabels_[i]->setJustificationType(juce::Justification::centredLeft);
            paramLabels_[i]->setInterceptsMouseClicks(false, false);  // Pass through for selection
            addAndMakeVisible(*paramLabels_[i]);

            paramSliders_[i] = std::make_unique<TextSlider>(TextSlider::Format::Decimal);
            paramSliders_[i]->setRange(0.0, 1.0, 0.01);
            paramSliders_[i]->setValue(0.5, juce::dontSendNotification);
            addAndMakeVisible(*paramSliders_[i]);
        }
    }

    ~DeviceSlotComponent() override = default;

    magda::DeviceId getDeviceId() const {
        return device_.id;
    }

    int getPreferredWidth() const override {
        return getTotalWidth(BASE_SLOT_WIDTH);
    }

    void updateFromDevice(const magda::DeviceInfo& device) {
        device_ = device;
        setNodeName(device.name);
        setBypassed(device.bypassed);
        onButton_->setToggleState(!device.bypassed, juce::dontSendNotification);
        onButton_->setActive(!device.bypassed);
        gainSlider_.setValue(device.gainDb, juce::dontSendNotification);
        repaint();
    }

  protected:
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override {
        // Header layout: [M] [Name...] [gain slider] [UI] [on]
        // Note: delete (X) is handled by NodeComponent on the right

        // Mod button on the left (before name)
        modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);

        // Power button on the right (before delete which is handled by parent)
        onButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);

        // UI button
        uiButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);

        // Gain slider takes some space on the right
        gainSlider_.setBounds(headerArea.removeFromRight(50));
        headerArea.removeFromRight(4);

        // Remaining space is for the name label (handled by NodeComponent)
    }

    // No footer for devices
    int getFooterHeight() const override {
        return 0;
    }

    // Devices show mod panel but not param/gain panels (params are inline)
    int getModPanelWidth() const override {
        return DEFAULT_PANEL_WIDTH;  // 60px
    }
    int getParamPanelWidth() const override {
        return 0;  // Params shown inline
    }
    int getGainPanelWidth() const override {
        return 0;  // Gain in header
    }

    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override {
        // Manufacturer label at top
        auto labelArea = contentArea.removeFromTop(12);
        auto textColour = isBypassed() ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                       : DarkTheme::getSecondaryTextColour();
        g.setColour(textColour);
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.drawText(device_.manufacturer, labelArea.reduced(2, 0), juce::Justification::centredLeft);
    }

    void resizedContent(juce::Rectangle<int> contentArea) override {
        // Skip manufacturer label area
        contentArea.removeFromTop(12);
        contentArea = contentArea.reduced(2, 0);

        // Update param fonts from debug settings
        auto labelFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamLabelFontSize());
        auto valueFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamValueFontSize());
        for (int i = 0; i < NUM_PARAMS; ++i) {
            paramLabels_[i]->setFont(labelFont);
            paramSliders_[i]->setFont(valueFont);
        }

        // Layout params in a 4-column grid, scaled to fit available space
        int numRows = (NUM_PARAMS + PARAMS_PER_ROW - 1) / PARAMS_PER_ROW;
        int cellWidth = contentArea.getWidth() / PARAMS_PER_ROW;
        int cellHeight = contentArea.getHeight() / numRows;
        int labelHeight = juce::jmin(10, cellHeight / 3);
        int sliderHeight = cellHeight - labelHeight - 2;

        for (int i = 0; i < NUM_PARAMS; ++i) {
            int row = i / PARAMS_PER_ROW;
            int col = i % PARAMS_PER_ROW;
            int x = contentArea.getX() + col * cellWidth;
            int y = contentArea.getY() + row * cellHeight;

            paramLabels_[i]->setBounds(x, y, cellWidth - 2, labelHeight);
            paramSliders_[i]->setBounds(x, y + labelHeight, cellWidth - 2, sliderHeight);
        }
    }

  private:
    ChainPanel& owner_;
    magda::DeviceInfo device_;

    // Header controls
    std::unique_ptr<magda::SvgButton> modButton_;
    TextSlider gainSlider_;
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> onButton_;

    std::unique_ptr<juce::Label> paramLabels_[NUM_PARAMS];
    std::unique_ptr<TextSlider> paramSliders_[NUM_PARAMS];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

//==============================================================================
// ElementSlotsContainer - Custom container that paints arrows between chain elements
//==============================================================================
class ChainPanel::ElementSlotsContainer : public juce::Component {
  public:
    explicit ElementSlotsContainer(ChainPanel& owner) : owner_(owner) {}

    void setElementSlots(const std::vector<std::unique_ptr<NodeComponent>>* slots) {
        elementSlots_ = slots;
    }

    void paint(juce::Graphics& g) override {
        if (!elementSlots_)
            return;

        // Draw arrows between elements
        int arrowY = getHeight() / 2;

        for (size_t i = 0; i < elementSlots_->size(); ++i) {
            auto& slot = (*elementSlots_)[i];
            int x = slot->getRight();  // Arrow starts after the slot

            // Draw arrow after each element
            g.setColour(DarkTheme::getSecondaryTextColour());
            int arrowStart = x + 4;
            int arrowEnd = x + 12;
            g.drawLine(static_cast<float>(arrowStart), static_cast<float>(arrowY),
                       static_cast<float>(arrowEnd), static_cast<float>(arrowY), 1.5f);
            // Arrow head
            g.drawLine(static_cast<float>(arrowEnd - 4), static_cast<float>(arrowY - 3),
                       static_cast<float>(arrowEnd), static_cast<float>(arrowY), 1.5f);
            g.drawLine(static_cast<float>(arrowEnd - 4), static_cast<float>(arrowY + 3),
                       static_cast<float>(arrowEnd), static_cast<float>(arrowY), 1.5f);
        }
    }

    void mouseDown(const juce::MouseEvent& /*e*/) override {
        // Click on empty area - clear device selection
        owner_.clearDeviceSelection();
    }

  private:
    ChainPanel& owner_;
    const std::vector<std::unique_ptr<NodeComponent>>* elementSlots_ = nullptr;
};

//==============================================================================
// ChainPanel
//==============================================================================

ChainPanel::ChainPanel() : elementSlotsContainer_(std::make_unique<ElementSlotsContainer>(*this)) {
    // No header - controls are on the chain row

    // Listen for debug settings changes
    DebugSettings::getInstance().addListener([this]() {
        // Force all element slots to update their fonts
        for (auto& slot : elementSlots_) {
            slot->resized();
            slot->repaint();
        }
        resized();
        repaint();
    });

    onLayoutChanged = [this]() {
        // Recalculate container size when a slot's size changes (e.g., panel toggle)
        resized();
        repaint();
        if (auto* parent = getParentComponent()) {
            parent->resized();
            parent->repaint();
        }
    };

    // Viewport for horizontal scrolling of element slots
    elementViewport_.setViewedComponent(elementSlotsContainer_.get(), false);
    elementViewport_.setScrollBarsShown(false, true);  // Horizontal only
    addAndMakeVisible(elementViewport_);

    // Add device button (inside the container, after all slots)
    addDeviceButton_.setButtonText("+");
    addDeviceButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    addDeviceButton_.setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getSecondaryTextColour());
    addDeviceButton_.onClick = [this]() { onAddDeviceClicked(); };
    addDeviceButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    elementSlotsContainer_->addAndMakeVisible(addDeviceButton_);

    setVisible(false);
}

ChainPanel::~ChainPanel() = default;

void ChainPanel::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    // Paint mod/macro panel at bottom if visible
    if (chainModPanelVisible_ || chainMacroPanelVisible_) {
        auto panelArea = contentArea;
        panelArea.removeFromTop(contentArea.getHeight() - MOD_MACRO_PANEL_HEIGHT);

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(panelArea);

        // Border on top
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(panelArea.getY(), static_cast<float>(panelArea.getX()),
                             static_cast<float>(panelArea.getRight()));

        panelArea = panelArea.reduced(8, 4);

        // Draw content based on which panel is visible
        if (chainModPanelVisible_) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
            g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
            g.drawText("MODULATORS", panelArea.removeFromTop(16), juce::Justification::centredLeft);

            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText("LFO, ADSR, Envelope Follower slots for this chain",
                       panelArea.removeFromTop(14), juce::Justification::centredLeft);
        }

        if (chainMacroPanelVisible_) {
            auto macroArea = panelArea;
            if (chainModPanelVisible_) {
                macroArea.removeFromTop(4);  // Gap after mod panel content
            }

            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
            g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
            g.drawText("MACROS", macroArea.removeFromTop(16), juce::Justification::centredLeft);

            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText("8 macro knobs for quick parameter access", macroArea.removeFromTop(14),
                       juce::Justification::centredLeft);
        }
    }
}

void ChainPanel::resizedContent(juce::Rectangle<int> contentArea) {
    // Reserve space at bottom for mod/macro panel if visible
    if (chainModPanelVisible_ || chainMacroPanelVisible_) {
        contentArea.removeFromBottom(MOD_MACRO_PANEL_HEIGHT);
    }

    // Viewport fills the remaining content area
    elementViewport_.setBounds(contentArea);

    // Calculate total width needed for all element slots
    int totalWidth = calculateTotalContentWidth();
    int containerHeight = contentArea.getHeight();

    // Account for horizontal scrollbar if needed
    if (totalWidth > contentArea.getWidth()) {
        containerHeight = contentArea.getHeight() - 8;  // Space for scrollbar
    }

    // Set container size and update element slots reference for arrow painting
    elementSlotsContainer_->setSize(totalWidth, containerHeight);
    elementSlotsContainer_->setElementSlots(&elementSlots_);

    // Layout element slots inside the container
    int x = 0;
    for (auto& slot : elementSlots_) {
        int slotWidth = slot->getPreferredWidth();
        slot->setBounds(x, 0, slotWidth, containerHeight);
        x += slotWidth + ARROW_WIDTH;
    }

    // Add device button after all slots
    addDeviceButton_.setBounds(x, (containerHeight - 20) / 2, 20, 20);
}

int ChainPanel::calculateTotalContentWidth() const {
    int totalWidth = 0;
    for (const auto& slot : elementSlots_) {
        totalWidth += slot->getPreferredWidth() + ARROW_WIDTH;
    }
    totalWidth += 30;  // Space for add device button
    return totalWidth;
}

int ChainPanel::getContentWidth() const {
    // Return the total content width (devices + arrows + add button)
    return juce::jmax(300, calculateTotalContentWidth());  // Minimum 300px
}

void ChainPanel::setMaxWidth(int maxWidth) {
    maxWidth_ = maxWidth;
}

void ChainPanel::onDeviceLayoutChanged() {
    // Recalculate container size and relayout
    resized();
    repaint();
    // Notify parent (RackComponent) that our preferred width may have changed
    if (onLayoutChanged) {
        onLayoutChanged();
    }
}

// Show a chain with full path context (for proper nesting)
void ChainPanel::showChain(const magda::ChainNodePath& chainPath) {
    DBG("ChainPanel::showChain - received path with " << chainPath.steps.size() << " steps");
    for (size_t i = 0; i < chainPath.steps.size(); ++i) {
        DBG("  step[" << i << "]: type=" << static_cast<int>(chainPath.steps[i].type)
                      << ", id=" << chainPath.steps[i].id);
    }

    chainPath_ = chainPath;
    trackId_ = chainPath.trackId;

    // Extract rackId and chainId from the path
    // The path should end with a Chain step
    if (!chainPath.steps.empty()) {
        for (const auto& step : chainPath.steps) {
            if (step.type == magda::ChainStepType::Rack) {
                rackId_ = step.id;
            } else if (step.type == magda::ChainStepType::Chain) {
                chainId_ = step.id;
            }
        }
    }

    hasChain_ = true;

    // Update name from chain data (using the top-level rack ID for now)
    // For deeply nested chains, we'd need to walk the path
    auto resolved = magda::TrackManager::getInstance().resolvePath(chainPath);
    DBG("  resolved.valid=" << (resolved.valid ? "yes" : "no")
                            << " resolved.chain=" << (resolved.chain ? "found" : "nullptr"));
    if (resolved.valid && resolved.chain) {
        setNodeName(resolved.chain->name);
        setBypassed(false);  // Chains don't have bypass yet
    }

    rebuildElementSlots();
    setVisible(true);
    resized();
    repaint();
}

// Legacy: show a chain by IDs (computes path internally)
void ChainPanel::showChain(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId) {
    // Build the path from IDs and call the path-based version
    auto chainPath = magda::ChainNodePath::chain(trackId, rackId, chainId);
    showChain(chainPath);
}

void ChainPanel::refresh() {
    if (!hasChain_)
        return;

    // Update name from chain data using path-based resolution
    auto resolved = magda::TrackManager::getInstance().resolvePath(chainPath_);
    if (resolved.valid && resolved.chain) {
        setNodeName(resolved.chain->name);
    }

    rebuildElementSlots();
    resized();
    repaint();
}

void ChainPanel::clear() {
    DBG("ChainPanel::clear() called - chainId=" << chainId_ << " rackId=" << rackId_);
    // Unfocus any child components before destroying them to prevent use-after-free
    unfocusAllComponents();

    hasChain_ = false;
    elementSlots_.clear();
    setVisible(false);
}

void ChainPanel::rebuildElementSlots() {
    if (!hasChain_) {
        unfocusAllComponents();
        elementSlots_.clear();
        return;
    }

    // Use path-based resolution to support nested chains at any depth
    auto resolved = magda::TrackManager::getInstance().resolvePath(chainPath_);
    if (!resolved.valid || !resolved.chain) {
        DBG("ChainPanel::rebuildElementSlots - chain not found via path!");
        unfocusAllComponents();
        elementSlots_.clear();
        return;
    }
    const auto* chain = resolved.chain;

    // Smart rebuild: preserve existing slots, only add/remove as needed
    std::vector<std::unique_ptr<NodeComponent>> newSlots;

    for (const auto& element : chain->elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);

            // Check if we already have a slot for this device
            DeviceSlotComponent* existingDeviceSlot = nullptr;
            size_t existingIndex = 0;
            for (size_t i = 0; i < elementSlots_.size(); ++i) {
                if (auto* deviceSlot = dynamic_cast<DeviceSlotComponent*>(elementSlots_[i].get())) {
                    if (deviceSlot->getDeviceId() == device.id) {
                        existingDeviceSlot = deviceSlot;
                        existingIndex = i;
                        break;
                    }
                }
            }

            if (existingDeviceSlot) {
                // Found existing slot - preserve it and update its data
                existingDeviceSlot->updateFromDevice(device);
                existingDeviceSlot->setNodePath(chainPath_.withDevice(device.id));
                newSlots.push_back(std::move(elementSlots_[existingIndex]));
                elementSlots_.erase(elementSlots_.begin() + static_cast<long>(existingIndex));
            } else {
                // Create new slot for new device
                auto slot = std::make_unique<DeviceSlotComponent>(*this, device);
                // Set the full path - this is used by all path-based operations
                slot->setNodePath(chainPath_.withDevice(device.id));
                elementSlotsContainer_->addAndMakeVisible(*slot);
                newSlots.push_back(std::move(slot));
            }
        } else if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);

            // Build the path for this nested rack
            auto nestedRackPath = chainPath_.withRack(rack.id);
            DBG("ChainPanel::rebuildElementSlots - creating nestedRackPath for rack id="
                << rack.id);
            DBG("  chainPath_ has " << chainPath_.steps.size()
                                    << " steps, trackId=" << chainPath_.trackId);
            DBG("  nestedRackPath has " << nestedRackPath.steps.size() << " steps");

            // Check if we already have a RackComponent for this rack
            RackComponent* existingRackComp = nullptr;
            size_t existingIndex = 0;
            for (size_t i = 0; i < elementSlots_.size(); ++i) {
                if (auto* rackComp = dynamic_cast<RackComponent*>(elementSlots_[i].get())) {
                    if (rackComp->getRackId() == rack.id) {
                        existingRackComp = rackComp;
                        existingIndex = i;
                        break;
                    }
                }
            }

            if (existingRackComp) {
                // Found existing RackComponent - preserve it and update its data
                existingRackComp->updateFromRack(rack);
                existingRackComp->setNodePath(nestedRackPath);
                newSlots.push_back(std::move(elementSlots_[existingIndex]));
                elementSlots_.erase(elementSlots_.begin() + static_cast<long>(existingIndex));
            } else {
                // Create new RackComponent for nested rack (with path context)
                auto rackComp = std::make_unique<RackComponent>(nestedRackPath, rack);
                rackComp->setNodePath(nestedRackPath);
                rackComp->onLayoutChanged = [this]() { onDeviceLayoutChanged(); };
                elementSlotsContainer_->addAndMakeVisible(*rackComp);
                newSlots.push_back(std::move(rackComp));
            }
        }
    }

    // Unfocus before destroying remaining old slots (elements that were removed)
    if (!elementSlots_.empty()) {
        unfocusAllComponents();
    }

    // Move new slots to member variable (old slots are destroyed here)
    elementSlots_ = std::move(newSlots);
}

void ChainPanel::onAddDeviceClicked() {
    if (!hasChain_)
        return;

    juce::PopupMenu menu;

    // Devices submenu
    juce::PopupMenu devicesMenu;
    devicesMenu.addItem(1, "Pro-Q 3");
    devicesMenu.addItem(2, "Pro-C 2");
    devicesMenu.addItem(3, "Saturn 2");
    devicesMenu.addItem(4, "Valhalla Room");
    devicesMenu.addItem(5, "Serum");
    menu.addSubMenu("Add Device", devicesMenu);

    menu.addSeparator();
    menu.addItem(100, "Create Rack");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result == 100) {
            // Create nested rack using path-based method for proper nesting support
            magda::TrackManager::getInstance().addRackToChainByPath(chainPath_);
            rebuildElementSlots();
            resized();
            repaint();
        } else if (result > 0 && result < 100) {
            magda::DeviceInfo device;
            switch (result) {
                case 1:
                    device.name = "Pro-Q 3";
                    device.manufacturer = "FabFilter";
                    break;
                case 2:
                    device.name = "Pro-C 2";
                    device.manufacturer = "FabFilter";
                    break;
                case 3:
                    device.name = "Saturn 2";
                    device.manufacturer = "FabFilter";
                    break;
                case 4:
                    device.name = "Valhalla Room";
                    device.manufacturer = "Valhalla DSP";
                    break;
                case 5:
                    device.name = "Serum";
                    device.manufacturer = "Xfer Records";
                    break;
            }
            device.format = magda::PluginFormat::VST3;
            magda::TrackManager::getInstance().addDeviceToChainByPath(chainPath_, device);
            rebuildElementSlots();
            resized();
            repaint();
        }
    });
}

void ChainPanel::setModPanelVisible(bool visible) {
    if (chainModPanelVisible_ != visible) {
        chainModPanelVisible_ = visible;
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void ChainPanel::setMacroPanelVisible(bool visible) {
    if (chainMacroPanelVisible_ != visible) {
        chainMacroPanelVisible_ = visible;
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void ChainPanel::clearDeviceSelection() {
    selectedDeviceId_ = magda::INVALID_DEVICE_ID;
    for (auto& slot : elementSlots_) {
        slot->setSelected(false);
    }
    if (onDeviceSelected) {
        onDeviceSelected(magda::INVALID_DEVICE_ID);
    }
    // Clear centralized selection so re-selecting the same node works
    magda::SelectionManager::getInstance().clearChainNodeSelection();
}

void ChainPanel::onDeviceSlotSelected(magda::DeviceId deviceId) {
    // Exclusive selection - deselect all others
    selectedDeviceId_ = deviceId;
    for (auto& slot : elementSlots_) {
        if (auto* deviceSlot = dynamic_cast<DeviceSlotComponent*>(slot.get())) {
            deviceSlot->setSelected(deviceSlot->getDeviceId() == deviceId);
        } else {
            slot->setSelected(false);
        }
    }
    if (onDeviceSelected) {
        onDeviceSelected(deviceId);
    }
}

}  // namespace magda::daw::ui
