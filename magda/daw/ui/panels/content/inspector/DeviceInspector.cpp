#include "DeviceInspector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

DeviceInspector::DeviceInspector() {
    chainNodeTypeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    chainNodeTypeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(chainNodeTypeLabel_);

    chainNodeNameLabel_.setText("Name", juce::dontSendNotification);
    chainNodeNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    chainNodeNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(chainNodeNameLabel_);

    chainNodeNameValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    chainNodeNameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(chainNodeNameValue_);

    latencyLabel_.setText("Latency", juce::dontSendNotification);
    latencyLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    latencyLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(latencyLabel_);

    latencyValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    latencyValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(latencyValue_);
}

DeviceInspector::~DeviceInspector() = default;

void DeviceInspector::onActivated() {
    // No listeners needed - updates come from parent InspectorContainer
}

void DeviceInspector::onDeactivated() {
    // No cleanup needed
}

void DeviceInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void DeviceInspector::resized() {
    auto bounds = getLocalBounds().reduced(10);

    if (!selectedChainNode_.isValid()) {
        return;
    }

    // Chain node type label
    chainNodeTypeLabel_.setBounds(bounds.removeFromTop(16));
    bounds.removeFromTop(4);

    // Chain node name
    chainNodeNameLabel_.setBounds(bounds.removeFromTop(16));
    chainNodeNameValue_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(16);

    // Latency (if visible)
    if (latencyLabel_.isVisible()) {
        latencyLabel_.setBounds(bounds.removeFromTop(16));
        latencyValue_.setBounds(bounds.removeFromTop(24));
    }
}

void DeviceInspector::setSelectedChainNode(const magda::ChainNodePath& path) {
    selectedChainNode_ = path;
    updateFromSelectedChainNode();
}

void DeviceInspector::updateFromSelectedChainNode() {
    bool hasSelection = selectedChainNode_.isValid();

    showDeviceControls(hasSelection);

    if (!hasSelection) {
        return;
    }

    auto& tm = magda::TrackManager::getInstance();
    auto type = selectedChainNode_.getType();

    juce::String typeStr;
    juce::String nameStr;
    double latency = 0.0;
    bool showLatency = false;

    // Check for display overrides (e.g., pad chain plugin info)
    auto& sm = magda::SelectionManager::getInstance();
    auto displayName = sm.getChainNodeDisplayName();
    auto displayType = sm.getChainNodeDisplayType();

    if (displayName.isNotEmpty()) {
        // Use override info (pad chain plugin)
        typeStr = displayType;
        nameStr = displayName;
    } else
        switch (type) {
            case magda::ChainNodeType::Device:
            case magda::ChainNodeType::TopLevelDevice: {
                auto* device = tm.getDeviceInChainByPath(selectedChainNode_);
                if (device) {
                    typeStr = device->getFormatString() +
                              (device->isInstrument ? " Instrument" : " Effect");
                    nameStr = device->name;
                    latency = tm.getDeviceLatencySeconds(selectedChainNode_);
                    showLatency = true;
                }
                break;
            }
            case magda::ChainNodeType::Rack: {
                auto* rack = tm.getRackByPath(selectedChainNode_);
                if (rack) {
                    typeStr = "Rack";
                    nameStr = rack->name;
                }
                break;
            }
            case magda::ChainNodeType::Chain: {
                typeStr = "Chain";
                nameStr = "Chain";
                break;
            }
            default:
                break;
        }

    chainNodeTypeLabel_.setText(typeStr, juce::dontSendNotification);
    chainNodeNameValue_.setText(nameStr, juce::dontSendNotification);

    if (showLatency && latency > 0.0) {
        auto latencyMs = latency * 1000.0;
        latencyValue_.setText(juce::String(latencyMs, 1) + " ms", juce::dontSendNotification);
        latencyLabel_.setVisible(true);
        latencyValue_.setVisible(true);
    } else {
        latencyLabel_.setVisible(showLatency);
        latencyValue_.setVisible(showLatency);
        if (showLatency) {
            latencyValue_.setText("0 ms", juce::dontSendNotification);
        }
    }

    resized();
}

void DeviceInspector::showDeviceControls(bool show) {
    chainNodeTypeLabel_.setVisible(show);
    chainNodeNameLabel_.setVisible(show);
    chainNodeNameValue_.setVisible(show);
    latencyLabel_.setVisible(false);
    latencyValue_.setVisible(false);
}

}  // namespace magda::daw::ui
