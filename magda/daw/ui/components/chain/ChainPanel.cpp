#include "ChainPanel.hpp"

#include "DeviceSlotComponent.hpp"
#include "NodeComponent.hpp"
#include "RackComponent.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "audio/MidiChordEnginePlugin.hpp"
#include "core/DeviceInfo.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/panels/content/PluginBrowserContent.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

//==============================================================================
// ZoomableViewport - Viewport that supports Cmd+scroll for zooming
//==============================================================================
class ChainPanel::ZoomableViewport : public juce::Viewport {
  public:
    explicit ZoomableViewport(ChainPanel& owner) : owner_(owner) {}

    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override {
        DBG("ZoomableViewport::mouseWheelMove - deltaY="
            << wheel.deltaY << " isCommandDown=" << (event.mods.isCommandDown() ? "yes" : "no"));

        // Cmd/Ctrl + scroll wheel = zoom
        if (event.mods.isCommandDown()) {
            float delta = wheel.deltaY > 0 ? ChainPanel::ZOOM_STEP : -ChainPanel::ZOOM_STEP;
            DBG("  -> Zooming by " << delta << " to " << (owner_.getZoomLevel() + delta));
            owner_.setZoomLevel(owner_.getZoomLevel() + delta);
        } else {
            // Normal scroll - let viewport handle horizontal scrolling
            Viewport::mouseWheelMove(event, wheel);
        }
    }

  private:
    ChainPanel& owner_;
};

//==============================================================================
// ElementSlotsContainer - Custom container that paints arrows between chain elements
//==============================================================================
class ChainPanel::ElementSlotsContainer : public juce::Component, public juce::DragAndDropTarget {
  public:
    explicit ElementSlotsContainer(ChainPanel& owner) : owner_(owner) {}

    void setElementSlots(const std::vector<std::unique_ptr<NodeComponent>>* slots) {
        elementSlots_ = slots;
    }

    void mouseDown(const juce::MouseEvent& /*e*/) override {
        // Click on empty area - clear device selection
        owner_.clearDeviceSelection();
    }

    void mouseMove(const juce::MouseEvent&) override {
        // Check if drop state is stale (drag was cancelled)
        checkAndResetStaleDropState();
    }

    void mouseEnter(const juce::MouseEvent&) override {
        // Check if drop state is stale (drag was cancelled while outside)
        checkAndResetStaleDropState();
    }

    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override {
        DBG("ElementSlotsContainer::mouseWheelMove - deltaY="
            << wheel.deltaY << " isCommandDown=" << (event.mods.isCommandDown() ? "yes" : "no"));

        // Cmd/Ctrl + scroll wheel = zoom
        if (event.mods.isCommandDown()) {
            float delta = wheel.deltaY > 0 ? ChainPanel::ZOOM_STEP : -ChainPanel::ZOOM_STEP;
            owner_.setZoomLevel(owner_.getZoomLevel() + delta);
        } else {
            // Normal scroll - let parent handle it (viewport scrolling)
            Component::mouseWheelMove(event, wheel);
        }
    }

    void paint(juce::Graphics& g) override {
        if (!elementSlots_)
            return;

        // Draw insertion indicator during drag (reorder or drop)
        if (owner_.dragInsertIndex_ >= 0 || owner_.dropInsertIndex_ >= 0) {
            int indicatorIndex =
                owner_.dragInsertIndex_ >= 0 ? owner_.dragInsertIndex_ : owner_.dropInsertIndex_;
            int indicatorX = owner_.calculateIndicatorX(indicatorIndex);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            g.fillRect(indicatorX - 2, 0, 4, getHeight());
        }

        // Draw ghost image during drag
        if (owner_.dragGhostImage_.isValid()) {
            g.setOpacity(0.6f);
            int ghostX = owner_.dragMousePos_.x - owner_.dragGhostImage_.getWidth() / 2;
            int ghostY = owner_.dragMousePos_.y - owner_.dragGhostImage_.getHeight() / 2;
            g.drawImageAt(owner_.dragGhostImage_, ghostX, ghostY);
            g.setOpacity(1.0f);
        }
    }

    // DragAndDropTarget implementation
    bool isInterestedInDragSource(const SourceDetails& details) override {
        // Accept plugin drops if we have a valid chain
        if (!owner_.hasChain_) {
            return false;
        }
        if (auto* obj = details.description.getDynamicObject()) {
            return obj->getProperty("type").toString() == "plugin";
        }
        return false;
    }

    void itemDragEnter(const SourceDetails& details) override {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(details.localPosition.x);
        owner_.startTimerHz(10);  // Start timer to detect stale drop state
        owner_.resized();         // Trigger relayout to add left padding
        repaint();
    }

    void itemDragMove(const SourceDetails& details) override {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(details.localPosition.x);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override {
        owner_.dropInsertIndex_ = -1;
        owner_.stopTimer();
        owner_.resized();  // Trigger relayout to remove left padding
        repaint();
    }

    void itemDropped(const SourceDetails& details) override {
        // Capture everything we need before touching owner_, because
        // addDeviceToChainByPath triggers a UI rebuild that may destroy
        // this container and the owning ChainPanel (use-after-free crash).
        magda::DeviceInfo device;
        bool validDrop = false;
        auto chainPath = owner_.chainPath_;
        int insertIndex = owner_.dropInsertIndex_ >= 0 ? owner_.dropInsertIndex_
                                                       : static_cast<int>(elementSlots_->size());

        if (auto* obj = details.description.getDynamicObject()) {
            device.name = obj->getProperty("name").toString().toStdString();
            device.manufacturer = obj->getProperty("manufacturer").toString().toStdString();
            auto uniqueId = obj->getProperty("uniqueId").toString();
            device.pluginId = uniqueId.isNotEmpty() ? uniqueId
                                                    : obj->getProperty("name").toString() + "_" +
                                                          obj->getProperty("format").toString();
            device.isInstrument = static_cast<bool>(obj->getProperty("isInstrument"));
            if (obj->getProperty("subcategory").toString() == "MIDI")
                device.deviceType = magda::DeviceType::MIDI;
            else if (device.isInstrument)
                device.deviceType = magda::DeviceType::Instrument;
            device.uniqueId = obj->getProperty("uniqueId").toString();
            device.fileOrIdentifier = obj->getProperty("fileOrIdentifier").toString();

            juce::String format = obj->getProperty("format").toString();
            if (format == "VST3") {
                device.format = magda::PluginFormat::VST3;
            } else if (format == "AU") {
                device.format = magda::PluginFormat::AU;
            } else if (format == "VST") {
                device.format = magda::PluginFormat::VST;
            } else if (format == "Internal") {
                device.format = magda::PluginFormat::Internal;
            }
            validDrop = true;
        }

        // Clear drop state before the TrackManager call (which triggers rebuild)
        owner_.dropInsertIndex_ = -1;
        owner_.stopTimer();

        if (validDrop) {
            DBG("Dropped plugin: " + juce::String(device.name) + " into chain at index " +
                juce::String(insertIndex));
            // This may destroy 'this' and owner_ — do not access any members after
            magda::TrackManager::getInstance().addDeviceToChainByPath(chainPath, device,
                                                                      insertIndex);
            return;
        }

        // Only reached if drop was not valid
        owner_.resized();
        repaint();
    }

  private:
    void checkAndResetStaleDropState() {
        if (owner_.dropInsertIndex_ >= 0) {
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                if (!container->isDragAndDropActive()) {
                    owner_.dropInsertIndex_ = -1;
                    owner_.resized();
                    repaint();
                }
            }
        }
    }

    ChainPanel& owner_;
    const std::vector<std::unique_ptr<NodeComponent>>* elementSlots_ = nullptr;
};

//==============================================================================
// ChainPanel
//==============================================================================

ChainPanel::ChainPanel()
    : elementViewport_(std::make_unique<ZoomableViewport>(*this)),
      elementSlotsContainer_(std::make_unique<ElementSlotsContainer>(*this)) {
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
    elementViewport_->setViewedComponent(elementSlotsContainer_.get(), false);
    elementViewport_->setScrollBarsShown(false, true);  // Horizontal only
    addAndMakeVisible(*elementViewport_);

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

ChainPanel::~ChainPanel() {
    stopTimer();
}

void ChainPanel::paintContent(juce::Graphics& /*g*/, juce::Rectangle<int> /*contentArea*/) {
    // Chain panels no longer have chain-level mods/macros - these are at rack level only
}

void ChainPanel::resizedContent(juce::Rectangle<int> contentArea) {
    // Viewport fills the content area
    elementViewport_->setBounds(contentArea);

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

    // Add left padding during drag/drop to show insertion indicator before first element
    bool isDraggingOrDropping = dragOriginalIndex_ >= 0 || dropInsertIndex_ >= 0;
    int x = isDraggingOrDropping ? DRAG_LEFT_PADDING : 0;

    // Layout element slots inside the container with zoom applied
    int scaledArrowWidth = getScaledWidth(ARROW_WIDTH);
    for (auto& slot : elementSlots_) {
        int slotWidth = getScaledWidth(slot->getPreferredWidth());
        slot->setBounds(x, 0, slotWidth, containerHeight);
        x += slotWidth + scaledArrowWidth;
    }

    // Add device button after all slots (not scaled)
    addDeviceButton_.setBounds(x, (containerHeight - 20) / 2, 20, 20);
}

int ChainPanel::calculateTotalContentWidth() const {
    // Add left padding during drag/drop to show insertion indicator before first element
    bool isDraggingOrDropping = dragOriginalIndex_ >= 0 || dropInsertIndex_ >= 0;
    int totalWidth = isDraggingOrDropping ? DRAG_LEFT_PADDING : 0;

    int scaledArrowWidth = getScaledWidth(ARROW_WIDTH);
    for (const auto& slot : elementSlots_) {
        totalWidth += getScaledWidth(slot->getPreferredWidth()) + scaledArrowWidth;
    }
    totalWidth += 30;  // Space for add device button (not scaled)
    return totalWidth;
}

int ChainPanel::getContentWidth() const {
    // Content width + NodeComponent's reduced(2,1) padding (4px horizontal)
    return calculateTotalContentWidth() + 4;
}

void ChainPanel::setMaxWidth(int maxWidth) {
    maxWidth_ = maxWidth;
}

void ChainPanel::setZoomLevel(float zoom) {
    DBG("ChainPanel::setZoomLevel - requested=" << zoom << " current=" << zoomLevel_);
    float newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, zoom);
    if (std::abs(zoomLevel_ - newZoom) > 0.001f) {
        zoomLevel_ = newZoom;
        DBG("  -> Zoom changed to " << zoomLevel_);
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    } else {
        DBG("  -> No change (clamped to " << newZoom << ")");
    }
}

void ChainPanel::resetZoom() {
    setZoomLevel(1.0f);
}

int ChainPanel::getScaledWidth(int width) const {
    return static_cast<int>(std::round(width * zoomLevel_));
}

void ChainPanel::mouseEnter(const juce::MouseEvent&) {
    DBG("ChainPanel::mouseEnter - visible=" << (isVisible() ? "yes" : "no")
                                            << " bounds=" << getBounds().toString());
}

void ChainPanel::mouseWheelMove(const juce::MouseEvent& event,
                                const juce::MouseWheelDetails& wheel) {
    // Option/Alt + scroll wheel = zoom (Cmd+scroll is intercepted by macOS)
    if (event.mods.isAltDown()) {
        float delta = wheel.deltaY > 0 ? ZOOM_STEP : -ZOOM_STEP;
        DBG("  -> Zooming to " << (zoomLevel_ + delta));
        setZoomLevel(zoomLevel_ + delta);
    } else {
        // Normal scroll - let viewport handle it
        NodeComponent::mouseWheelMove(event, wheel);
    }
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
        setBypassed(resolved.chain->bypassed);
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

void ChainPanel::updateParamIndicators() {
    // Repaint all device slots to update parameter modulation indicators
    for (auto& slot : elementSlots_) {
        if (auto* deviceSlot = dynamic_cast<DeviceSlotComponent*>(slot.get())) {
            deviceSlot->repaint();
        }
    }
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
                auto slot = std::make_unique<DeviceSlotComponent>(device);
                // Set the full path - this is used by all path-based operations
                slot->setNodePath(chainPath_.withDevice(device.id));
                // Wire up device-specific callbacks
                slot->onDeviceLayoutChanged = [this]() { onDeviceLayoutChanged(); };
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

    // Wire up drag-to-reorder callbacks for all element slots
    // Use SafePointer because moveElementInChainByPath triggers rebuild which may destroy this
    auto safeThis = juce::Component::SafePointer<ChainPanel>(this);

    for (auto& slot : elementSlots_) {
        // Wire up zoom callback (Cmd+scroll on any node forwards to ChainPanel)
        slot->onZoomDelta = [safeThis](float delta) {
            if (safeThis != nullptr) {
                safeThis->setZoomLevel(safeThis->getZoomLevel() + delta);
            }
        };

        slot->onDragStart = [safeThis](NodeComponent* node, const juce::MouseEvent&) {
            if (safeThis == nullptr)
                return;
            safeThis->draggedElement_ = node;
            safeThis->dragOriginalIndex_ = safeThis->findElementIndex(node);
            safeThis->dragInsertIndex_ = safeThis->dragOriginalIndex_;
            // Capture ghost image and make original semi-transparent
            safeThis->dragGhostImage_ = node->createComponentSnapshot(node->getLocalBounds());
            node->setAlpha(0.4f);
            safeThis->startTimerHz(10);  // Start timer to detect stale drag state
            // Re-layout to add left padding for drop indicator
            safeThis->resized();
        };

        slot->onDragMove = [safeThis](NodeComponent*, const juce::MouseEvent& e) {
            if (safeThis == nullptr)
                return;
            auto pos = e.getEventRelativeTo(safeThis->elementSlotsContainer_.get()).getPosition();
            safeThis->dragInsertIndex_ = safeThis->calculateInsertIndex(pos.x);
            safeThis->dragMousePos_ = pos;
            safeThis->elementSlotsContainer_->repaint();
        };

        slot->onDragEnd = [safeThis](NodeComponent* node, const juce::MouseEvent&) {
            if (safeThis == nullptr)
                return;

            // Restore alpha and clear ghost
            node->setAlpha(1.0f);
            safeThis->dragGhostImage_ = juce::Image();
            safeThis->stopTimer();

            int elementCount = static_cast<int>(safeThis->elementSlots_.size());
            if (safeThis->dragOriginalIndex_ >= 0 && safeThis->dragInsertIndex_ >= 0 &&
                safeThis->dragOriginalIndex_ != safeThis->dragInsertIndex_) {
                // Convert insert position to target index
                int targetIndex = safeThis->dragInsertIndex_;
                if (safeThis->dragInsertIndex_ > safeThis->dragOriginalIndex_) {
                    targetIndex = safeThis->dragInsertIndex_ - 1;
                }
                targetIndex = juce::jlimit(0, elementCount - 1, targetIndex);
                if (targetIndex != safeThis->dragOriginalIndex_) {
                    // Capture chainPath before the move (in case safeThis becomes invalid)
                    auto chainPath = safeThis->chainPath_;
                    int fromIndex = safeThis->dragOriginalIndex_;

                    // Clear state before the move (which triggers rebuild)
                    safeThis->draggedElement_ = nullptr;
                    safeThis->dragOriginalIndex_ = -1;
                    safeThis->dragInsertIndex_ = -1;

                    // Perform the move - this may destroy safeThis
                    magda::TrackManager::getInstance().moveElementInChainByPath(
                        chainPath, fromIndex, targetIndex);
                    return;  // Don't access safeThis after this point
                }
            }

            // Only reached if no move happened
            safeThis->draggedElement_ = nullptr;
            safeThis->dragOriginalIndex_ = -1;
            safeThis->dragInsertIndex_ = -1;
            // Re-layout and repaint to remove left padding and indicator
            safeThis->resized();
            safeThis->elementSlotsContainer_->repaint();
        };
    }
}

void ChainPanel::onAddDeviceClicked() {
    if (!hasChain_)
        return;

    juce::PopupMenu menu;

    // --- Internal (built-in) plugins from shared list ---
    auto internals = magda::daw::ui::PluginBrowserContent::getInternalPlugins();
    juce::PopupMenu internalMenu;
    int itemId = 1;
    for (const auto& entry : internals) {
        internalMenu.addItem(itemId++, entry.name);
    }
    menu.addSubMenu("Internal", internalMenu);

    // --- External plugins from KnownPluginList ---
    juce::Array<juce::PluginDescription> externalPlugins;
    if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
            magda::TrackManager::getInstance().getAudioEngine())) {
        auto& knownPlugins = engine->getKnownPluginList();
        externalPlugins = knownPlugins.getTypes();
    }

    if (!externalPlugins.isEmpty()) {
        // Group by manufacturer
        std::map<juce::String, juce::PopupMenu> byManufacturer;
        for (int i = 0; i < externalPlugins.size(); ++i) {
            const auto& desc = externalPlugins[i];
            auto manufacturer = desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
            byManufacturer[manufacturer].addItem(1000 + static_cast<int>(i), desc.name);
        }
        for (auto& [manufacturer, subMenu] : byManufacturer) {
            menu.addSubMenu(manufacturer, subMenu);
        }
    }

    menu.addSeparator();
    menu.addItem(10000, "Create Rack");

    auto safeThis = juce::Component::SafePointer<ChainPanel>(this);
    auto chainPath = chainPath_;

    auto capturedPlugins =
        std::make_shared<juce::Array<juce::PluginDescription>>(std::move(externalPlugins));
    auto capturedInternals =
        std::make_shared<std::vector<magda::daw::ui::PluginBrowserInfo>>(std::move(internals));

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, chainPath, capturedPlugins,
                                                    capturedInternals](int result) {
        if (result == 0)
            return;

        if (result == 10000) {
            magda::TrackManager::getInstance().addRackToChainByPath(chainPath);
            if (safeThis != nullptr) {
                safeThis->rebuildElementSlots();
                safeThis->resized();
                safeThis->repaint();
            }
            return;
        }

        magda::DeviceInfo device;

        if (result >= 1 && result <= static_cast<int>(capturedInternals->size())) {
            // Internal plugin
            const auto& entry = (*capturedInternals)[static_cast<size_t>(result - 1)];
            device.name = entry.name;
            device.manufacturer = "MAGDA";
            device.pluginId = entry.uniqueId;
            device.isInstrument = entry.category == "Instrument";
            if (entry.subcategory == "MIDI")
                device.deviceType = magda::DeviceType::MIDI;
            else if (entry.category == "Instrument")
                device.deviceType = magda::DeviceType::Instrument;
            device.format = magda::PluginFormat::Internal;
        } else if (result >= 1000) {
            // External plugin
            int idx = result - 1000;
            if (idx < 0 || idx >= static_cast<int>(capturedPlugins->size()))
                return;
            const auto& desc = (*capturedPlugins)[idx];
            device.name = desc.name;
            device.manufacturer = desc.manufacturerName;
            device.pluginId = desc.createIdentifierString();
            device.isInstrument = desc.isInstrument;
            device.uniqueId = desc.createIdentifierString();
            device.fileOrIdentifier = desc.fileOrIdentifier;
            if (desc.pluginFormatName == "VST3")
                device.format = magda::PluginFormat::VST3;
            else if (desc.pluginFormatName == "AU" || desc.pluginFormatName == "AudioUnit")
                device.format = magda::PluginFormat::AU;
            else if (desc.pluginFormatName == "VST")
                device.format = magda::PluginFormat::VST;
            else
                device.format = magda::PluginFormat::Internal;
        } else {
            return;
        }

        magda::TrackManager::getInstance().addDeviceToChainByPath(chainPath, device);
        if (safeThis != nullptr) {
            safeThis->rebuildElementSlots();
            safeThis->resized();
            safeThis->repaint();
        }
    });
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

int ChainPanel::findElementIndex(NodeComponent* element) const {
    for (size_t i = 0; i < elementSlots_.size(); ++i) {
        if (elementSlots_[i].get() == element) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ChainPanel::calculateInsertIndex(int mouseX) const {
    // Find insert position based on mouse X and element midpoints
    for (size_t i = 0; i < elementSlots_.size(); ++i) {
        int midX = elementSlots_[i]->getX() + elementSlots_[i]->getWidth() / 2;
        if (mouseX < midX) {
            return static_cast<int>(i);
        }
    }
    // After last element
    return static_cast<int>(elementSlots_.size());
}

int ChainPanel::calculateIndicatorX(int index) const {
    // Before first element - center in the drag padding area
    if (index == 0) {
        return DRAG_LEFT_PADDING / 2;
    }

    // After previous element (use scaled arrow width)
    if (index > 0 && index <= static_cast<int>(elementSlots_.size())) {
        int scaledArrowWidth = getScaledWidth(ARROW_WIDTH);
        return elementSlots_[index - 1]->getRight() + scaledArrowWidth / 2;
    }

    // Fallback
    return DRAG_LEFT_PADDING / 2;
}

void ChainPanel::timerCallback() {
    // Check if internal drag state is stale (drag was cancelled)
    if (dragInsertIndex_ >= 0 || draggedElement_ != nullptr) {
        // Check if any mouse button is still down - if not, the drag was cancelled
        if (!juce::Desktop::getInstance().getMainMouseSource().isDragging()) {
            if (draggedElement_) {
                draggedElement_->setAlpha(1.0f);
            }
            draggedElement_ = nullptr;
            dragOriginalIndex_ = -1;
            dragInsertIndex_ = -1;
            dragGhostImage_ = juce::Image();
            stopTimer();
            resized();
            elementSlotsContainer_->repaint();
            return;
        }
    }

    // Check if external drop state is stale (drag was cancelled)
    if (dropInsertIndex_ >= 0) {
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(
                elementSlotsContainer_.get())) {
            if (!container->isDragAndDropActive()) {
                dropInsertIndex_ = -1;
                stopTimer();
                resized();
                elementSlotsContainer_->repaint();
                return;
            }
        }
    }

    // No stale state, stop the timer
    if (dragInsertIndex_ < 0 && draggedElement_ == nullptr && dropInsertIndex_ < 0) {
        stopTimer();
    }
}

}  // namespace magda::daw::ui
