#include "DeviceInspector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

DeviceInspector::DeviceInspector() {
    // ========================================================================
    // Chain node properties section
    // ========================================================================

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

    // ========================================================================
    // Device parameters section
    // ========================================================================

    deviceParamsLabel_.setText("Parameters", juce::dontSendNotification);
    deviceParamsLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    deviceParamsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(deviceParamsLabel_);

    deviceParamsViewport_.setViewedComponent(&deviceParamsContainer_, false);
    deviceParamsViewport_.setScrollBarsShown(true, false);
    addChildComponent(deviceParamsViewport_);
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

    // Device parameters section (if visible)
    if (deviceParamsLabel_.isVisible()) {
        deviceParamsLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);

        // Viewport takes remaining space
        deviceParamsViewport_.setBounds(bounds);
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

    // TODO: Extract from InspectorContent::updateFromSelectedChainNode()
    // - Get chain node info from SelectionManager/DeviceManager
    // - Update chainNodeTypeLabel_ with node type
    // - Update chainNodeNameValue_ with node name
    // - Call rebuildParameterControls() if device has parameters

    resized();
}

void DeviceInspector::showDeviceControls(bool show) {
    chainNodeTypeLabel_.setVisible(show);
    chainNodeNameLabel_.setVisible(show);
    chainNodeNameValue_.setVisible(show);
    deviceParamsLabel_.setVisible(show);
    deviceParamsViewport_.setVisible(show);
}

void DeviceInspector::rebuildParameterControls() {
    // TODO: Extract from InspectorContent
    // - Clear deviceParamsContainer_
    // - Get device parameters from DeviceManager
    // - Create parameter controls (sliders, buttons, etc.)
    // - Add to deviceParamsContainer_
    // - Update container size for scrolling
}

}  // namespace magda::daw::ui
