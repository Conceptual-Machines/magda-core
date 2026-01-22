#include "ChainTreeDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda {

// ============================================================================
// TreeViewItem Subclasses
// ============================================================================

/**
 * @brief Base class for all chain tree items
 */
class ChainTreeItemBase : public juce::TreeViewItem {
  public:
    explicit ChainTreeItemBase(const juce::String& text, const juce::String& icon = "",
                               const ChainNodePath& path = {})
        : text_(text), icon_(icon), path_(path) {}

    bool mightContainSubItems() override {
        return false;  // Override in containers
    }

    void paintItem(juce::Graphics& g, int width, int height) override {
        auto bounds = juce::Rectangle<int>(0, 0, width, height);

        // Highlight if selected
        if (isSelected()) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
            g.fillRect(bounds);
        }

        // Draw expand/collapse indicator for containers
        if (mightContainSubItems()) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.setFont(FontManager::getInstance().getUIFont(12.0f));
            juce::String indicator =
                isOpen() ? juce::String::fromUTF8("▼ ") : juce::String::fromUTF8("▶ ");
            g.drawText(indicator, bounds.removeFromLeft(16), juce::Justification::centred);
        } else {
            bounds.removeFromLeft(16);  // Indent leaf nodes
        }

        // Draw icon if present
        if (icon_.isNotEmpty()) {
            g.setFont(FontManager::getInstance().getUIFont(12.0f));
            g.drawText(icon_, bounds.removeFromLeft(20), juce::Justification::centred);
        }

        // Draw text
        g.setColour(getItemColour());
        g.setFont(getItemFont());
        g.drawText(text_, bounds.reduced(4, 0), juce::Justification::centredLeft);

        // Draw secondary text if present
        if (secondaryText_.isNotEmpty()) {
            auto secondaryBounds = bounds.removeFromRight(juce::jmin(100, bounds.getWidth() / 2));
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(secondaryText_, secondaryBounds, juce::Justification::centredRight);
        }
    }

    void itemClicked(const juce::MouseEvent&) override {
        // Toggle open/close for containers
        if (mightContainSubItems()) {
            setOpen(!isOpen());
        }

        // Select this node in the main UI if it has a valid path
        if (path_.isValid()) {
            SelectionManager::getInstance().selectChainNode(path_);
        }
    }

    int getItemHeight() const override {
        return 24;
    }

    juce::String getUniqueName() const override {
        return text_ + "_" + juce::String(reinterpret_cast<juce::pointer_sized_int>(this));
    }

    const ChainNodePath& getPath() const {
        return path_;
    }

  protected:
    virtual juce::Colour getItemColour() const {
        return DarkTheme::getTextColour();
    }

    virtual juce::Font getItemFont() const {
        return FontManager::getInstance().getUIFont(12.0f);
    }

    void setSecondaryText(const juce::String& text) {
        secondaryText_ = text;
    }

    juce::String text_;
    juce::String icon_;
    juce::String secondaryText_;
    ChainNodePath path_;
};

/**
 * @brief Track item (root node) - no selection, just a container
 */
class TrackTreeItem : public ChainTreeItemBase {
  public:
    explicit TrackTreeItem(const juce::String& trackName, TrackId trackId)
        : ChainTreeItemBase(trackName, juce::String::fromUTF8("🎚️")), trackId_(trackId) {}

    bool mightContainSubItems() override {
        return true;
    }

    void itemClicked(const juce::MouseEvent&) override {
        // Toggle open/close but select the track
        setOpen(!isOpen());
        SelectionManager::getInstance().selectTrack(trackId_);
    }

  protected:
    juce::Font getItemFont() const override {
        return FontManager::getInstance().getUIFontBold(12.0f);
    }

  private:
    TrackId trackId_;
};

/**
 * @brief Device item (leaf node)
 */
class DeviceTreeItem : public ChainTreeItemBase {
  public:
    DeviceTreeItem(const DeviceInfo& device, const ChainNodePath& path)
        : ChainTreeItemBase(device.name, juce::String::fromUTF8("📦"), path) {
        setSecondaryText(device.manufacturer);
    }

    bool mightContainSubItems() override {
        return false;
    }
};

/**
 * @brief Rack item (container node)
 */
class RackTreeItem : public ChainTreeItemBase {
  public:
    RackTreeItem(const juce::String& rackName, const ChainNodePath& path)
        : ChainTreeItemBase(rackName, juce::String::fromUTF8("🗂️"), path) {}

    bool mightContainSubItems() override {
        return true;
    }

  protected:
    juce::Colour getItemColour() const override {
        return DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    }
};

/**
 * @brief Chain item (container within rack)
 */
class ChainTreeItem : public ChainTreeItemBase {
  public:
    ChainTreeItem(const juce::String& chainName, const ChainNodePath& path)
        : ChainTreeItemBase(chainName, juce::String::fromUTF8("🔗"), path) {}

    bool mightContainSubItems() override {
        return true;
    }

  protected:
    juce::Colour getItemColour() const override {
        return DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    }
};

// ============================================================================
// Content Component
// ============================================================================

class ChainTreeDialog::ContentComponent : public juce::Component,
                                          public TrackManagerListener,
                                          public SelectionManagerListener {
  public:
    explicit ContentComponent(TrackId trackId) : trackId_(trackId) {
        // Setup tree view
        treeView_.setColour(juce::TreeView::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        treeView_.setColour(juce::TreeView::linesColourId, DarkTheme::getBorderColour());
        treeView_.setDefaultOpenness(true);
        treeView_.setMultiSelectEnabled(false);
        treeView_.setOpenCloseButtonsVisible(false);  // We draw our own
        treeView_.setIndentSize(20);
        addAndMakeVisible(treeView_);

        // Info label
        infoLabel_.setText("Click an item to select it in the chain view",
                           juce::dontSendNotification);
        infoLabel_.setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        infoLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(infoLabel_);

        // Register listeners
        TrackManager::getInstance().addListener(this);
        SelectionManager::getInstance().addListener(this);

        buildTree();
        setSize(400, 500);
    }

    ~ContentComponent() override {
        treeView_.setRootItem(nullptr);
        TrackManager::getInstance().removeListener(this);
        SelectionManager::getInstance().removeListener(this);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        infoLabel_.setBounds(bounds.removeFromBottom(25));
        bounds.removeFromBottom(5);
        treeView_.setBounds(bounds);
    }

    // TrackManagerListener
    void tracksChanged() override {
        // Check if our track still exists
        if (!TrackManager::getInstance().getTrack(trackId_)) {
            // Track was deleted, close the dialog
            if (auto* parent = findParentComponentOfClass<ChainTreeDialog>()) {
                parent->closeButtonPressed();
            }
            return;
        }
        buildTree();
    }

    void trackDevicesChanged(TrackId trackId) override {
        if (trackId == trackId_) {
            buildTree();
        }
    }

    // SelectionManagerListener
    void selectionTypeChanged(SelectionType /*newType*/) override {}

    void chainNodeSelectionChanged(const ChainNodePath& path) override {
        // Find and select the tree item matching this path
        if (path.trackId != trackId_) {
            return;  // Different track
        }

        // Find the matching tree item
        if (auto* item = findTreeItemByPath(rootItem_.get(), path)) {
            item->setSelected(true, true);
            // Ensure parent items are expanded
            auto* parent = item->getParentItem();
            while (parent) {
                parent->setOpen(true);
                parent = parent->getParentItem();
            }
        }
    }

  private:
    ChainTreeItemBase* findTreeItemByPath(juce::TreeViewItem* item, const ChainNodePath& path) {
        if (!item)
            return nullptr;

        // Check if this item matches
        if (auto* chainItem = dynamic_cast<ChainTreeItemBase*>(item)) {
            if (chainItem->getPath() == path) {
                return chainItem;
            }
        }

        // Search children
        for (int i = 0; i < item->getNumSubItems(); ++i) {
            if (auto* found = findTreeItemByPath(item->getSubItem(i), path)) {
                return found;
            }
        }

        return nullptr;
    }
    void buildTree() {
        treeView_.setRootItem(nullptr);
        rootItem_.reset();

        const auto* track = TrackManager::getInstance().getTrack(trackId_);
        if (!track) {
            return;
        }

        // Create root item for track
        auto root = std::make_unique<TrackTreeItem>(track->name, trackId_);

        // Add top-level devices
        for (const auto& device : track->devices) {
            auto path = ChainNodePath::topLevelDevice(trackId_, device.id);
            root->addSubItem(new DeviceTreeItem(device, path));
        }

        // Add racks with their chains
        for (const auto& rack : track->racks) {
            auto rackPath = ChainNodePath::rack(trackId_, rack.id);
            root->addSubItem(buildRackItem(rack, rackPath));
        }

        rootItem_ = std::move(root);
        treeView_.setRootItem(rootItem_.get());
        treeView_.setRootItemVisible(true);

        // Expand all items
        expandAllItems(rootItem_.get());
    }

    juce::TreeViewItem* buildRackItem(const RackInfo& rack, const ChainNodePath& rackPath) {
        auto* rackItem = new RackTreeItem(rack.name, rackPath);

        for (const auto& chain : rack.chains) {
            auto chainPath = rackPath.withChain(chain.id);
            auto* chainItem = new ChainTreeItem(chain.name, chainPath);

            for (const auto& element : chain.elements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    auto devicePath = chainPath.withDevice(device.id);
                    chainItem->addSubItem(new DeviceTreeItem(device, devicePath));
                } else if (isRack(element)) {
                    // Recursive: nested rack
                    const auto& nestedRack = getRack(element);
                    auto nestedRackPath = chainPath.withRack(nestedRack.id);
                    chainItem->addSubItem(buildRackItem(nestedRack, nestedRackPath));
                }
            }

            rackItem->addSubItem(chainItem);
        }

        return rackItem;
    }

    void expandAllItems(juce::TreeViewItem* item) {
        if (!item)
            return;

        item->setOpen(true);

        for (int i = 0; i < item->getNumSubItems(); ++i) {
            expandAllItems(item->getSubItem(i));
        }
    }

    TrackId trackId_;
    juce::TreeView treeView_;
    juce::Label infoLabel_;
    std::unique_ptr<juce::TreeViewItem> rootItem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
};

// ============================================================================
// ChainTreeDialog
// ============================================================================

ChainTreeDialog::ChainTreeDialog(TrackId trackId)
    : DialogWindow("Chain Tree", DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND), true) {
    content_ = std::make_unique<ContentComponent>(trackId);
    setContentOwned(content_.release(), true);
    centreWithSize(400, 500);
    setResizable(true, true);
    setUsingNativeTitleBar(true);
}

ChainTreeDialog::~ChainTreeDialog() = default;

void ChainTreeDialog::closeButtonPressed() {
    setVisible(false);
}

void ChainTreeDialog::show(TrackId trackId) {
    if (trackId == INVALID_TRACK_ID) {
        return;
    }

    // Get track name for dialog title
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track) {
        return;
    }

    auto* dialog = new ChainTreeDialog(trackId);
    dialog->setName("Chain Tree - " + track->name);
    dialog->setVisible(true);
    dialog->toFront(true);
}

}  // namespace magda
