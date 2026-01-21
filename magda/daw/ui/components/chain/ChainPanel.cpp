#include "ChainPanel.hpp"

#include <BinaryData.h>

#include "NodeComponent.hpp"
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

    DeviceSlotComponent(ChainPanel& owner, magda::TrackId trackId, magda::RackId rackId,
                        magda::ChainId chainId, const magda::DeviceInfo& device)
        : owner_(owner),
          trackId_(trackId),
          rackId_(rackId),
          chainId_(chainId),
          device_(device),
          gainSlider_(TextSlider::Format::Decibels) {
        setNodeName(device.name);
        setBypassed(device.bypassed);

        // Hide built-in bypass button - we'll add our own in the header
        setBypassButtonVisible(false);

        // Set up callbacks
        onDeleteClicked = [this]() {
            magda::TrackManager::getInstance().removeDeviceFromChain(trackId_, rackId_, chainId_,
                                                                     device_.id);
        };

        onModPanelToggled = [this](bool /*visible*/) {
            if (auto* parent = getParentComponent()) {
                parent->resized();
                parent->repaint();
            }
        };

        onLayoutChanged = [this]() {
            // Notify parent that our size may have changed
            if (auto* parent = getParentComponent()) {
                parent->resized();
                parent->repaint();
            }
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

        // Macro button (toggle param panel) - link icon
        macroButton_ = std::make_unique<magda::SvgButton>("Macro", BinaryData::link_bright_svg,
                                                          BinaryData::link_bright_svgSize);
        macroButton_->setClickingTogglesState(true);
        macroButton_->setToggleState(paramPanelVisible_, juce::dontSendNotification);
        macroButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
        macroButton_->setActiveColor(juce::Colours::white);
        macroButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        macroButton_->setActive(paramPanelVisible_);
        macroButton_->onClick = [this]() {
            macroButton_->setActive(macroButton_->getToggleState());
            paramPanelVisible_ = macroButton_->getToggleState();
            if (onParamPanelToggled)
                onParamPanelToggled(paramPanelVisible_);
        };
        addAndMakeVisible(*macroButton_);

        // Gain text slider in header
        gainSlider_.setRange(-60.0, 12.0, 0.1);
        gainSlider_.setValue(device_.gainDb, juce::dontSendNotification);
        gainSlider_.onValueChanged = [this](double value) {
            if (auto* dev = magda::TrackManager::getInstance().getDeviceInChain(
                    trackId_, rackId_, chainId_, device_.id)) {
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
        onButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        onButton_->setActive(!device.bypassed);
        onButton_->onClick = [this]() {
            bool active = onButton_->getToggleState();
            onButton_->setActive(active);
            setBypassed(!active);  // Active = not bypassed
            magda::TrackManager::getInstance().setDeviceInChainBypassed(trackId_, rackId_, chainId_,
                                                                        device_.id, !active);
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

    int getPreferredWidth() const {
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
        // Header layout: [M] [P] [Name...] [gain slider] [UI] [on]
        // Note: delete (X) is handled by NodeComponent on the right

        // Mod button on the left (before name)
        modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);

        // Macro button next to mod
        macroButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
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
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    magda::DeviceInfo device_;

    // Header controls
    std::unique_ptr<magda::SvgButton> modButton_;
    std::unique_ptr<magda::SvgButton> macroButton_;
    TextSlider gainSlider_;
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> onButton_;

    std::unique_ptr<juce::Label> paramLabels_[NUM_PARAMS];
    std::unique_ptr<TextSlider> paramSliders_[NUM_PARAMS];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

//==============================================================================
// ChainPanel
//==============================================================================

ChainPanel::ChainPanel() {
    // No header - controls are on the chain row

    // Listen for debug settings changes
    DebugSettings::getInstance().addListener([this]() {
        // Force all device slots to update their fonts
        for (auto& slot : deviceSlots_) {
            slot->resized();
            slot->repaint();
        }
        resized();
        repaint();
    });

    onLayoutChanged = [this]() {
        if (auto* parent = getParentComponent()) {
            parent->resized();
            parent->repaint();
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

}  // namespace magda::daw::ui
