// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/SamplerZoneList.hpp"

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

SamplerZoneList::SamplerZoneList() {
    addButton_.setTooltip("Add an empty zone; drop a sample on it to fill it");
    addButton_.onClick = [this] {
        if (onAdd)
            onAdd();
    };
    content_.addAndMakeVisible(addButton_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(6);
    addAndMakeVisible(viewport_);
}

void SamplerZoneList::setZones(const std::vector<Zone>& zones, int selected) {
    // Rows are kept while the count holds, so a drag on one survives a refresh.
    if (rows_.size() != zones.size()) {
        rows_.clear();
        for (size_t i = 0; i < zones.size(); ++i) {
            auto row = std::make_unique<SamplerZoneRow>();
            row->onSelect = [this](int index) {
                if (onSelect)
                    onSelect(index);
            };
            row->onEdit = [this](int index, const Zone& zone) {
                if (onEdit)
                    onEdit(index, zone);
            };
            row->onRemove = [this](int index) {
                if (onRemove)
                    onRemove(index);
            };
            content_.addAndMakeVisible(*row);
            rows_.push_back(std::move(row));
        }
    }
    for (size_t i = 0; i < zones.size(); ++i)
        rows_[i]->setZone(zones[i], static_cast<int>(i), static_cast<int>(i) == selected);
    resized();
}

int SamplerZoneList::rowAt(juce::Point<int> position) const {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i]->getScreenBounds().contains(localPointToGlobal(position)))
            return static_cast<int>(i);
    return -1;
}

juce::Rectangle<int> SamplerZoneList::tabBounds(SamplerZoneRow::View view) const {
    const int width = getWidth() / 3;
    return juce::Rectangle<int>(width * static_cast<int>(view), 0, width, kTabsHeight);
}

void SamplerZoneList::resized() {
    const auto area = getLocalBounds().withTrimmedTop(kTabsHeight + 4);
    if (area.isEmpty())
        return;
    constexpr int kGap = 4;
    constexpr int kAddHeight = 22;
    const int content =
        static_cast<int>(rows_.size()) * (SamplerZoneRow::kHeight + kGap) + kAddHeight;
    viewport_.setBounds(area);
    const int width = area.getWidth() - (content > area.getHeight() ? 8 : 0);
    content_.setSize(width, content);
    auto list = content_.getLocalBounds();
    for (auto& row : rows_) {
        row->setView(view_);
        row->setBounds(list.removeFromTop(SamplerZoneRow::kHeight));
        list.removeFromTop(kGap);
    }
    addButton_.setBounds(list.removeFromTop(kAddHeight));
}

void SamplerZoneList::mouseDown(const juce::MouseEvent& e) {
    for (const auto view :
         {SamplerZoneRow::View::Mix, SamplerZoneRow::View::Key, SamplerZoneRow::View::Velocity})
        if (tabBounds(view).contains(e.getPosition())) {
            view_ = view;
            resized();
            repaint();
            return;
        }
}

void SamplerZoneList::paint(juce::Graphics& g) {
    // The views, tabbed as a rack's chain list is.
    g.setFont(FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.08f));
    for (const auto& [view, name] :
         {std::pair{SamplerZoneRow::View::Mix, "MIX"}, std::pair{SamplerZoneRow::View::Key, "KEY"},
          std::pair{SamplerZoneRow::View::Velocity, "VEL"}}) {
        const auto tab = tabBounds(view);
        const bool active = view == view_;
        g.setColour(ActiveTheme::getColour(active ? ActiveTheme::DEVICE_VALUE_TEXT
                                                  : ActiveTheme::DEVICE_DIM2));
        g.drawText(name, tab, juce::Justification::centred, false);
        if (active) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
            g.fillRect(tab.withTop(tab.getBottom() - 2));
        }
    }
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.fillRect(getLocalBounds().withHeight(kTabsHeight).withTop(kTabsHeight - 1));
}

}  // namespace magda::daw::ui
