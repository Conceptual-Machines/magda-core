#include "SegmentedChoice.hpp"

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

namespace {

constexpr float kInset = 3.0f;
constexpr float kSegmentPadding = 10.0f;

juce::Font segmentFont() {
    return FontManager::getInstance().getMonoFont(11.0f).withExtraKerningFactor(0.08f);
}

}  // namespace

void SegmentedChoice::setOptions(const juce::StringArray& options) {
    if (options == options_)
        return;
    options_ = options;
    repaint();
}

void SegmentedChoice::setSelectedIndex(int index) {
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
}

int SegmentedChoice::getPreferredWidth() const {
    float width = 2.0f * kInset;
    for (const auto& option : options_)
        width += juce::GlyphArrangement::getStringWidth(segmentFont(), option.toUpperCase()) +
                 2.0f * kSegmentPadding;
    return juce::roundToInt(width);
}

juce::Rectangle<float> SegmentedChoice::segmentBounds(int index) const {
    auto area = getLocalBounds().toFloat().reduced(kInset);
    const auto scale =
        area.getWidth() / juce::jmax(1.0f, static_cast<float>(getPreferredWidth()) - 2.0f * kInset);
    for (int i = 0; i < options_.size(); ++i) {
        const auto width =
            (juce::GlyphArrangement::getStringWidth(segmentFont(), options_[i].toUpperCase()) +
             2.0f * kSegmentPadding) *
            scale;
        auto segment = area.removeFromLeft(width);
        if (i == index)
            return segment;
    }
    return {};
}

void SegmentedChoice::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    g.setFont(segmentFont());
    for (int i = 0; i < options_.size(); ++i) {
        const auto segment = segmentBounds(i);
        if (i == selected_) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
            g.fillRoundedRectangle(segment, 3.0f);
        }
        g.setColour(ActiveTheme::getColour(i == selected_ ? ActiveTheme::DEVICE_TEXT
                                                          : ActiveTheme::DEVICE_DIM));
        g.drawText(options_[i].toUpperCase(), segment, juce::Justification::centred, false);
    }
}

void SegmentedChoice::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < options_.size(); ++i) {
        if (segmentBounds(i).contains(e.position) && i != selected_) {
            setSelectedIndex(i);
            if (onChange)
                onChange(i);
            return;
        }
    }
}

}  // namespace magda
