#include "IconSelector.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

void IconSelector::addOption(const char* svgData, int svgSize, const juce::String& tooltip) {
    auto icon = juce::Drawable::createFromImageData(svgData, svgSize);
    if (icon) {
        icon->replaceColour(juce::Colours::black, DarkTheme::getSecondaryTextColour());
    }
    options_.push_back({std::move(icon), {}, tooltip});
}

void IconSelector::addTextOption(const juce::String& text, const juce::String& tooltip) {
    options_.push_back({nullptr, text, tooltip.isEmpty() ? text : tooltip});
}

void IconSelector::setSelectedIndex(int index, juce::NotificationType notification) {
    if (options_.empty())
        return;
    index = juce::jlimit(0, static_cast<int>(options_.size()) - 1, index);
    if (index == selectedIndex_)
        return;
    selectedIndex_ = index;
    repaint();
    if (notification != juce::dontSendNotification && onChange)
        onChange(selectedIndex_);
}

void IconSelector::paint(juce::Graphics& g) {
    auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    auto textPrimary = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    auto textSecondary = DarkTheme::getSecondaryTextColour();

    for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
        auto bounds = getOptionBounds(i);
        bool selected = (i == selectedIndex_);
        bool hovered = (i == hoveredIndex_);
        auto& opt = options_[static_cast<size_t>(i)];

        // Background for selected item
        if (selected) {
            g.setColour(accent.withAlpha(0.35f));
            g.fillRoundedRectangle(bounds.toFloat(), 2.0f);
            g.setColour(accent.withAlpha(0.6f));
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.0f, 1.0f);
        } else if (hovered) {
            g.setColour(accent.withAlpha(0.15f));
            g.fillRoundedRectangle(bounds.toFloat(), 2.0f);
        }

        if (opt.icon) {
            // Draw SVG icon
            auto iconArea = bounds.reduced(3).toFloat();
            float opacity = selected ? 1.0f : (hovered ? 0.8f : 0.45f);

            if (selected) {
                auto copy = opt.icon->createCopy();
                copy->replaceColour(textSecondary, textPrimary);
                copy->drawWithin(g, iconArea, juce::RectanglePlacement::centred, opacity);
            } else {
                opt.icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred, opacity);
            }
        } else if (opt.text.isNotEmpty()) {
            // Draw text option
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            float opacity = selected ? 1.0f : (hovered ? 0.8f : 0.5f);
            g.setColour((selected ? textPrimary : textSecondary).withAlpha(opacity));
            g.drawText(opt.text, bounds, juce::Justification::centred, false);
        }
    }
}

void IconSelector::mouseDown(const juce::MouseEvent& e) {
    int idx = hitTestOption(e.getPosition());
    if (idx >= 0)
        setSelectedIndex(idx);
}

void IconSelector::mouseMove(const juce::MouseEvent& e) {
    int idx = hitTestOption(e.getPosition());
    if (idx != hoveredIndex_) {
        hoveredIndex_ = idx;
        repaint();

        if (idx >= 0 && !options_[static_cast<size_t>(idx)].tooltip.isEmpty())
            setTooltip(options_[static_cast<size_t>(idx)].tooltip);
        else
            setTooltip({});
    }
}

void IconSelector::mouseExit(const juce::MouseEvent&) {
    if (hoveredIndex_ != -1) {
        hoveredIndex_ = -1;
        repaint();
    }
}

juce::Rectangle<int> IconSelector::getOptionBounds(int index) const {
    int n = static_cast<int>(options_.size());
    if (n == 0)
        return {};
    int totalW = getWidth();
    int h = getHeight();
    int cellW = totalW / n;
    return {index * cellW, 0, cellW, h};
}

int IconSelector::hitTestOption(juce::Point<int> pos) const {
    for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
        if (getOptionBounds(i).contains(pos))
            return i;
    }
    return -1;
}

}  // namespace magda::daw::ui
