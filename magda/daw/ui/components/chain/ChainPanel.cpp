#include "ChainPanel.hpp"

#include "NodeComponent.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

//==============================================================================
// DeviceSlotComponent - Device display within chain panel (inherits from NodeComponent)
//==============================================================================
class ChainPanel::DeviceSlotComponent : public NodeComponent {
  public:
    DeviceSlotComponent(ChainPanel& owner, magda::TrackId trackId, magda::RackId rackId,
                        magda::ChainId chainId, const magda::DeviceInfo& device)
        : owner_(owner), trackId_(trackId), rackId_(rackId), chainId_(chainId), device_(device) {
        setNodeName(device.name);
        setBypassed(device.bypassed);

        // Set up callbacks
        onBypassChanged = [this](bool bypassed) {
            magda::TrackManager::getInstance().setDeviceInChainBypassed(trackId_, rackId_, chainId_,
                                                                        device_.id, bypassed);
        };

        onDeleteClicked = [this]() {
            magda::TrackManager::getInstance().removeDeviceFromChain(trackId_, rackId_, chainId_,
                                                                     device_.id);
        };

        onLayoutChanged = [this]() {
            // Notify parent that our size may have changed
            if (auto* parent = getParentComponent()) {
                parent->resized();
                parent->repaint();
            }
        };

        // Hide param button - params shown at chain level instead
        setParamButtonVisible(false);
    }

    magda::DeviceId getDeviceId() const {
        return device_.id;
    }

    int getPreferredWidth() const {
        return getTotalWidth(BASE_SLOT_WIDTH);
    }

    void updateFromDevice(const magda::DeviceInfo& device) {
        device_ = device;
        setNodeName(device.name);
        setBypassed(device.bypassed);
        repaint();
    }

    static constexpr int BASE_SLOT_WIDTH = 100;  // Base width without panels

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override {
        // Device name in content area
        auto textColour = isBypassed() ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                       : DarkTheme::getTextColour();
        g.setColour(textColour);
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText(device_.manufacturer, contentArea.reduced(4), juce::Justification::centred);
    }

  private:
    ChainPanel& owner_;
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    magda::DeviceInfo device_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

//==============================================================================
// ChainPanel
//==============================================================================

ChainPanel::ChainPanel() {
    // No header - controls are on the chain row

    onLayoutChanged = [this]() {
        if (auto* parent = getParentComponent()) {
            parent->resized();
            parent->repaint();
        }
    };

    // Hide macro buttons when param panel is hidden
    onParamPanelToggled = [this](bool visible) {
        if (!visible) {
            for (auto& btn : macroButtons_) {
                if (btn)
                    btn->setVisible(false);
            }
        }
    };

    // Add device button
    addDeviceButton_.setButtonText("+");
    addDeviceButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    addDeviceButton_.setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getSecondaryTextColour());
    addDeviceButton_.onClick = [this]() { onAddDeviceClicked(); };
    addDeviceButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(addDeviceButton_);

    // === MACRO BUTTONS (for param panel) ===
    for (int i = 0; i < 4; ++i) {
        macroButtons_[i] = std::make_unique<juce::TextButton>("+");
        macroButtons_[i]->setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
        macroButtons_[i]->setColour(juce::TextButton::textColourOffId,
                                    DarkTheme::getSecondaryTextColour());
        macroButtons_[i]->onClick = [this, i]() {
            juce::PopupMenu menu;
            menu.addSectionHeader("Create Chain Macro " + juce::String(i + 1));
            menu.addItem(1, "Link to device parameter...");
            menu.addItem(2, "Create empty macro");
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, i](int result) {
                if (result == 1) {
                    // TODO: Open parameter browser to link to device in this chain
                    macroButtons_[i]->setButtonText("M" + juce::String(i + 1));
                } else if (result == 2) {
                    macroButtons_[i]->setButtonText("M" + juce::String(i + 1));
                }
            });
        };
        addChildComponent(*macroButtons_[i]);
    }

    setVisible(false);
}

ChainPanel::~ChainPanel() = default;

void ChainPanel::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    if (!hasChain_)
        return;

    // Draw arrows between devices (use actual slot bounds for positioning)
    int arrowWidth = 16;
    int arrowY = contentArea.getCentreY();

    for (size_t i = 0; i < deviceSlots_.size(); ++i) {
        auto& slot = deviceSlots_[i];
        int x = slot->getRight();  // Arrow starts after the slot

        // Draw arrow after each device
        g.setColour(DarkTheme::getSecondaryTextColour());
        int arrowStart = x + 4;
        int arrowEnd = x + arrowWidth - 4;
        g.drawLine(static_cast<float>(arrowStart), static_cast<float>(arrowY),
                   static_cast<float>(arrowEnd), static_cast<float>(arrowY), 1.5f);
        // Arrow head
        g.drawLine(static_cast<float>(arrowEnd - 4), static_cast<float>(arrowY - 3),
                   static_cast<float>(arrowEnd), static_cast<float>(arrowY), 1.5f);
        g.drawLine(static_cast<float>(arrowEnd - 4), static_cast<float>(arrowY + 3),
                   static_cast<float>(arrowEnd), static_cast<float>(arrowY), 1.5f);
    }
}

void ChainPanel::resizedContent(juce::Rectangle<int> contentArea) {
    int x = contentArea.getX();
    int arrowWidth = 16;

    for (auto& slot : deviceSlots_) {
        int slotWidth = slot->getPreferredWidth();
        slot->setBounds(x, contentArea.getY(), slotWidth, contentArea.getHeight());
        x += slotWidth + arrowWidth;
    }

    // Add device button after all slots
    addDeviceButton_.setBounds(x, contentArea.getY() + (contentArea.getHeight() - 20) / 2, 20, 20);
}

void ChainPanel::showChain(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId) {
    trackId_ = trackId;
    rackId_ = rackId;
    chainId_ = chainId;
    hasChain_ = true;

    // Update name from chain data
    const auto* chain = magda::TrackManager::getInstance().getChain(trackId, rackId, chainId);
    if (chain) {
        setNodeName(chain->name);
        setBypassed(false);  // Chains don't have bypass yet
    }

    rebuildDeviceSlots();
    setVisible(true);
    resized();
    repaint();
}

void ChainPanel::refresh() {
    if (!hasChain_)
        return;

    // Update name from chain data
    const auto* chain = magda::TrackManager::getInstance().getChain(trackId_, rackId_, chainId_);
    if (chain) {
        setNodeName(chain->name);
    }

    rebuildDeviceSlots();
    resized();
    repaint();
}

void ChainPanel::clear() {
    // Unfocus any child components before destroying them to prevent use-after-free
    unfocusAllComponents();

    hasChain_ = false;
    deviceSlots_.clear();
    setVisible(false);
}

void ChainPanel::rebuildDeviceSlots() {
    if (!hasChain_) {
        unfocusAllComponents();
        deviceSlots_.clear();
        return;
    }

    const auto* chain = magda::TrackManager::getInstance().getChain(trackId_, rackId_, chainId_);
    if (!chain) {
        unfocusAllComponents();
        deviceSlots_.clear();
        return;
    }

    // Smart rebuild: preserve existing slots, only add/remove as needed
    std::vector<std::unique_ptr<DeviceSlotComponent>> newSlots;

    for (const auto& device : chain->devices) {
        // Check if we already have a slot for this device
        std::unique_ptr<DeviceSlotComponent> existingSlot;
        for (auto it = deviceSlots_.begin(); it != deviceSlots_.end(); ++it) {
            if ((*it)->getDeviceId() == device.id) {
                // Found existing slot - preserve it and update its data
                existingSlot = std::move(*it);
                deviceSlots_.erase(it);
                existingSlot->updateFromDevice(device);
                break;
            }
        }

        if (existingSlot) {
            newSlots.push_back(std::move(existingSlot));
        } else {
            // Create new slot for new device
            auto slot =
                std::make_unique<DeviceSlotComponent>(*this, trackId_, rackId_, chainId_, device);
            addAndMakeVisible(*slot);
            newSlots.push_back(std::move(slot));
        }
    }

    // Unfocus before destroying remaining old slots (devices that were removed)
    if (!deviceSlots_.empty()) {
        unfocusAllComponents();
    }

    // Move new slots to member variable (old slots are destroyed here)
    deviceSlots_ = std::move(newSlots);
}

void ChainPanel::onAddDeviceClicked() {
    if (!hasChain_)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Pro-Q 3");
    menu.addItem(2, "Pro-C 2");
    menu.addItem(3, "Saturn 2");
    menu.addItem(4, "Valhalla Room");
    menu.addItem(5, "Serum");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result > 0) {
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
            magda::TrackManager::getInstance().addDeviceToChain(trackId_, rackId_, chainId_,
                                                                device);
            rebuildDeviceSlots();
            resized();
            repaint();
        }
    });
}

void ChainPanel::paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Override: draw "MACRO" label instead of "PRM"
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("MACRO", panelArea.removeFromTop(16), juce::Justification::centred);
}

void ChainPanel::resizedParamPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip label
    panelArea = panelArea.reduced(2);

    // 2x2 grid of macro buttons
    int btnSize = (panelArea.getWidth() - 2) / 2;
    int row = 0, col = 0;
    for (int i = 0; i < 4; ++i) {
        int x = panelArea.getX() + col * (btnSize + 2);
        int y = panelArea.getY() + row * (btnSize + 2);
        macroButtons_[i]->setBounds(x, y, btnSize, btnSize);
        macroButtons_[i]->setVisible(true);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }
}

}  // namespace magda::daw::ui
