#include "DeviceInspector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/InternalDeviceKind.hpp"
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

    categoryLabel_.setText("Category", juce::dontSendNotification);
    categoryLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    categoryLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(categoryLabel_);

    categoryValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    categoryValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(categoryValue_);

    codenameLabel_.setText("Codename", juce::dontSendNotification);
    codenameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    codenameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(codenameLabel_);

    codenameValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    codenameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(codenameValue_);

    descriptionLabel_.setText("Description", juce::dontSendNotification);
    descriptionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    descriptionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(descriptionLabel_);

    descriptionValue_.setBaseFont(FontManager::getInstance().getUIFont(12.0f));
    descriptionValue_.setBaseColour(DarkTheme::getTextColour());
    addChildComponent(descriptionValue_);
}

DeviceInspector::~DeviceInspector() {
    magda::TrackManager::getInstance().removeListener(this);
}

void DeviceInspector::onActivated() {
    // Selection updates come from the parent InspectorContainer, but property
    // edits (e.g. a chain rename) only fire on TrackManager, so listen here to
    // keep the displayed name/props live.
    magda::TrackManager::getInstance().addListener(this);
}

void DeviceInspector::onDeactivated() {
    magda::TrackManager::getInstance().removeListener(this);
}

void DeviceInspector::trackPropertyChanged(int trackId) {
    if (selectedChainNode_.isValid() && selectedChainNode_.trackId == trackId)
        updateFromSelectedChainNode();
}

void DeviceInspector::trackDevicesChanged(int trackId) {
    if (selectedChainNode_.isValid() && selectedChainNode_.trackId == trackId)
        updateFromSelectedChainNode();
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

    if (categoryLabel_.isVisible()) {
        auto labelRow = bounds.removeFromTop(16);
        auto valueRow = bounds.removeFromTop(24);
        const int gap = 12;
        const int columnWidth = (labelRow.getWidth() - gap) / 2;

        chainNodeNameLabel_.setBounds(labelRow.removeFromLeft(columnWidth));
        labelRow.removeFromLeft(gap);
        categoryLabel_.setBounds(labelRow);

        chainNodeNameValue_.setBounds(valueRow.removeFromLeft(columnWidth));
        valueRow.removeFromLeft(gap);
        categoryValue_.setBounds(valueRow);
        bounds.removeFromTop(16);
    } else {
        // Chain node name
        chainNodeNameLabel_.setBounds(bounds.removeFromTop(16));
        chainNodeNameValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(16);
    }

    if (codenameLabel_.isVisible()) {
        codenameLabel_.setBounds(bounds.removeFromTop(16));
        codenameValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(10);
    }

    if (descriptionLabel_.isVisible()) {
        descriptionLabel_.setBounds(bounds.removeFromTop(16));
        descriptionValue_.setBounds(bounds.removeFromTop(160));
        bounds.removeFromTop(16);
    }

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
    const magda::InternalDeviceMetadata* metadata = nullptr;

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
                    metadata = magda::getInternalDeviceMetadataForPluginId(device->pluginId);
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
                if (const auto* chain = tm.getChainByPath(selectedChainNode_))
                    nameStr = chain->name;
                break;
            }
            default:
                break;
        }

    chainNodeTypeLabel_.setText(typeStr, juce::dontSendNotification);
    chainNodeNameValue_.setText(nameStr, juce::dontSendNotification);

    if (metadata != nullptr) {
        // Spec literals are UTF-8 (em-dash, ring operator, ...). juce::String's
        // default const char* ctor asserts on non-7-bit bytes, so go via
        // fromUTF8 here. Same pattern for category and codename for safety.
        const juce::String category = juce::String::fromUTF8(metadata->category);
        const juce::String description = juce::String::fromUTF8(metadata->description);
        categoryValue_.setText(category, juce::dontSendNotification);
        descriptionValue_.setStyledText(description);
        descriptionValue_.setTooltip(stripStyleTags(description));
        categoryLabel_.setVisible(category.isNotEmpty());
        categoryValue_.setVisible(category.isNotEmpty());
        descriptionLabel_.setVisible(description.isNotEmpty());
        descriptionValue_.setVisible(description.isNotEmpty());

        const juce::String codename = juce::String::fromUTF8(metadata->codename);
        codenameValue_.setText(codename, juce::dontSendNotification);
        codenameLabel_.setVisible(codename.isNotEmpty());
        codenameValue_.setVisible(codename.isNotEmpty());
    } else {
        categoryLabel_.setVisible(false);
        categoryValue_.setVisible(false);
        codenameLabel_.setVisible(false);
        codenameValue_.setVisible(false);
        descriptionLabel_.setVisible(false);
        descriptionValue_.setVisible(false);
        categoryValue_.setText({}, juce::dontSendNotification);
        codenameValue_.setText({}, juce::dontSendNotification);
        descriptionValue_.setStyledText({});
        descriptionValue_.setTooltip({});
    }

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
    categoryLabel_.setVisible(false);
    categoryValue_.setVisible(false);
    codenameLabel_.setVisible(false);
    codenameValue_.setVisible(false);
    descriptionLabel_.setVisible(false);
    descriptionValue_.setVisible(false);
}

}  // namespace magda::daw::ui
