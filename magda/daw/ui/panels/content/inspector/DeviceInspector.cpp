// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceInspector.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/DeviceCatalogMetadata.hpp"
#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kPad = 14;
constexpr int kKickerHeight = 14;
constexpr int kNameHeight = 24;
constexpr int kFactsHeight = 52;
constexpr int kSectionLabelHeight = 14;

juce::Font kickerFont() {
    return FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f);
}

juce::String sidechainSummary(const magda::DeviceInfo& device) {
    if (!device.sidechainPort.declared())
        return {};
    if (device.sidechain.isConfigured()) {
        const auto* source =
            magda::TrackManager::getInstance().getTrack(device.sidechain.sourceTrackId);
        const auto name = source != nullptr ? source->name : juce::String("track");
        return "From " + name + (device.sidechain.enabled ? "" : " (off)");
    }
    if (device.sidechainPort.kind == magda::SidechainPort::Kind::MIDI)
        return "MIDI, not set";
    return "Audio, not set";
}
}  // namespace

DeviceInspector::DeviceInspector() {
    descriptionValue_.setBaseFont(FontManager::getInstance().getUIFont(12.5f));
    descriptionValue_.setBaseColour(ActiveTheme::getTextColour());
    addChildComponent(descriptionValue_);

    modulation_.onPreferredHeightChanged = [this] {
        resized();
        repaint();
    };
    addChildComponent(modulation_);
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

void DeviceInspector::deviceModifiersChanged(magda::TrackId trackId) {
    if (selectedChainNode_.isValid() && selectedChainNode_.trackId == trackId)
        modulation_.refresh();
}

void DeviceInspector::modulationNamesChanged(magda::TrackId trackId) {
    deviceModifiersChanged(trackId);
}

void DeviceInspector::macroValueChanged(magda::TrackId trackId, magda::ChainScope, int, int,
                                        float) {
    if (selectedChainNode_.isValid() && selectedChainNode_.trackId == trackId)
        modulation_.refreshMacroValues();
}

void DeviceInspector::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getBackgroundColour());
    if (!selectedChainNode_.isValid())
        return;

    const auto border = ActiveTheme::getBorderColour();
    const auto dim = ActiveTheme::getColour(ActiveTheme::TEXT_DIM);

    // Header: type kicker over the name.
    g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
    g.fillRect(headerBounds_);
    auto header = headerBounds_.reduced(kPad);
    g.setFont(kickerFont());
    g.setColour(dim);
    g.drawText(kicker_.toUpperCase(), header.removeFromTop(kKickerHeight),
               juce::Justification::centredLeft, true);
    header.removeFromTop(4);
    g.setFont(FontManager::getInstance().getUIFontBold(17.0f));
    g.setColour(ActiveTheme::getTextColour());
    g.drawText(name_, header.removeFromTop(kNameHeight), juce::Justification::centredLeft, true);
    g.setColour(border);
    g.drawHorizontalLine(headerBounds_.getBottom(), 0.0f, static_cast<float>(getWidth()));

    // Facts: equal cells, label over value, split by hairlines.
    if (!facts_.empty()) {
        const int cellWidth = factsBounds_.getWidth() / static_cast<int>(facts_.size());
        auto row = factsBounds_;
        for (size_t i = 0; i < facts_.size(); ++i) {
            auto cell = i + 1 == facts_.size() ? row : row.removeFromLeft(cellWidth);
            if (i > 0) {
                g.setColour(border);
                g.drawVerticalLine(cell.getX(), static_cast<float>(cell.getY()),
                                   static_cast<float>(cell.getBottom()));
            }
            auto text = cell.reduced(kPad, 9);
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.setColour(ActiveTheme::getSecondaryTextColour());
            g.drawText(facts_[i].label, text.removeFromTop(15), juce::Justification::centredLeft,
                       true);
            g.setFont(facts_[i].mono ? FontManager::getInstance().getMonoFont(12.5f)
                                     : FontManager::getInstance().getUIFont(13.0f));
            g.setColour(ActiveTheme::getTextColour());
            g.drawText(facts_[i].value, text, juce::Justification::centredLeft, true);
        }
        g.setColour(border);
        g.drawHorizontalLine(factsBounds_.getBottom(), 0.0f, static_cast<float>(getWidth()));
    }

    if (modulationRuleY_ >= 0) {
        g.setColour(border);
        g.drawHorizontalLine(modulationRuleY_, 0.0f, static_cast<float>(getWidth()));
    }

    if (!aboutLabelBounds_.isEmpty()) {
        // Same face and colour as the clip and track inspectors' section headings.
        g.setFont(kickerFont());
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.drawText("ABOUT", aboutLabelBounds_, juce::Justification::centredLeft, false);
    }
}

void DeviceInspector::resized() {
    headerBounds_ = {};
    factsBounds_ = {};
    aboutLabelBounds_ = {};
    modulationRuleY_ = -1;
    modulation_.setVisible(selectedChainNode_.isValid() && modulation_.hasContent());
    if (!selectedChainNode_.isValid()) {
        setPreferredHeight(0);
        return;
    }

    // Laid out top-down from y = 0 with no height limit; a taller result scrolls the panel.
    int y = 0;
    const auto take = [this, &y](int height) {
        const juce::Rectangle<int> area(0, y, getWidth(), height);
        y += height;
        return area;
    };

    headerBounds_ = take(kPad + kKickerHeight + 4 + kNameHeight + kPad);
    take(1);
    if (!facts_.empty()) {
        factsBounds_ = take(kFactsHeight);
        take(1);
    }

    if (modulation_.isVisible()) {
        take(8);
        modulation_.setBounds(take(modulation_.getPreferredHeight()).reduced(kPad, 0));
        take(8);
        modulationRuleY_ = y;
        take(1);
    }

    if (descriptionValue_.isVisible()) {
        const int width = getWidth() - 2 * kPad;
        aboutLabelBounds_ = {kPad, y + kPad, width, kSectionLabelHeight};
        descriptionValue_.setBounds(kPad, aboutLabelBounds_.getBottom() + 8, width,
                                    descriptionValue_.getHeightForWidth(width) + 4);
        y = descriptionValue_.getBottom() + kPad;
    }
    setPreferredHeight(y);
}

void DeviceInspector::setSelectedChainNode(const magda::ChainNodePath& path) {
    selectedChainNode_ = path;
    updateFromSelectedChainNode();
}

void DeviceInspector::updateFromSelectedChainNode() {
    kicker_.clear();
    name_.clear();
    facts_.clear();
    juce::String description;

    if (selectedChainNode_.isValid()) {
        auto& tm = magda::TrackManager::getInstance();

        // Check for display overrides (e.g., pad chain plugin info)
        auto& sm = magda::SelectionManager::getInstance();
        auto displayName = sm.getChainNodeDisplayName();

        if (displayName.isNotEmpty()) {
            kicker_ = sm.getChainNodeDisplayType();
            name_ = std::move(displayName);
        } else {
            switch (selectedChainNode_.getType()) {
                case magda::ChainNodeType::Device:
                case magda::ChainNodeType::TopLevelDevice: {
                    const auto* device = tm.getDeviceInChainByPath(selectedChainNode_);
                    if (device == nullptr)
                        break;
                    kicker_ = device->getFormatString() +
                              (device->isInstrument ? " Instrument" : " Effect");
                    if (device->format != magda::PluginFormat::Internal &&
                        device->manufacturer.isNotEmpty())
                        kicker_ += juce::String::fromUTF8(" \xc2\xb7 ") + device->manufacturer;
                    name_ = device->name;

                    // Spec literals are UTF-8 (em-dash, ring operator, ...), so read via fromUTF8.
                    const auto metadata = magda::daw::findDeviceCatalogMetadata(device->pluginId);
                    if (metadata) {
                        if (const auto category = juce::String::fromUTF8(metadata.browserCategory);
                            category.isNotEmpty())
                            facts_.push_back({"Category", category});
                        description = juce::String::fromUTF8(metadata.description);
                    }
                    const double latencyMs =
                        tm.getDeviceLatencySeconds(selectedChainNode_) * 1000.0;
                    facts_.push_back({"Latency",
                                      latencyMs > 0.0 ? juce::String(latencyMs, 1) + " ms"
                                                      : juce::String("0 ms"),
                                      true});
                    if (const auto sidechain = sidechainSummary(*device); sidechain.isNotEmpty())
                        facts_.push_back({"Sidechain", sidechain});
                    break;
                }
                case magda::ChainNodeType::Rack:
                    if (const auto* rack = tm.getRackByPath(selectedChainNode_)) {
                        kicker_ = "Rack";
                        name_ = rack->name;
                    }
                    break;
                case magda::ChainNodeType::Chain:
                    kicker_ = "Chain";
                    if (const auto* chain = tm.getChainByPath(selectedChainNode_))
                        name_ = chain->displayName();
                    break;
                default:
                    break;
            }
        }
    }

    descriptionValue_.setStyledText(description);
    descriptionValue_.setTooltip(stripStyleTags(description));
    descriptionValue_.setVisible(description.isNotEmpty());
    modulation_.setOwner(selectedChainNode_);

    resized();
    repaint();
}

}  // namespace magda::daw::ui
