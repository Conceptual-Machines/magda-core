#include "SegmentedChoice.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

namespace {

constexpr float kInset = 3.0f;
constexpr float kSegmentPadding = 10.0f;
constexpr float kIconSegmentWidth = 30.0f;

juce::Font segmentFont() {
    return FontManager::getInstance().getMonoFont(11.0f).withExtraKerningFactor(0.08f);
}

/// The option's picture in a unit box, y down, or nothing when it has none.
std::optional<juce::Path> iconFor(const juce::String& option) {
    const auto name = option.toLowerCase();
    juce::Path path;
    const auto sampled = [&path](auto y) {
        for (int i = 0; i <= 32; ++i) {
            const float x = static_cast<float>(i) / 32.0f;
            if (i == 0)
                path.startNewSubPath(x, y(x));
            else
                path.lineTo(x, y(x));
        }
    };
    if (name == "lp" || name == "hc") {
        path.startNewSubPath(0.0f, 0.35f);
        path.lineTo(0.45f, 0.35f);
        path.quadraticTo(0.7f, 0.35f, 1.0f, 0.95f);
    } else if (name == "hp" || name == "lc") {
        path.startNewSubPath(0.0f, 0.95f);
        path.quadraticTo(0.3f, 0.35f, 0.55f, 0.35f);
        path.lineTo(1.0f, 0.35f);
    } else if (name == "bp") {
        path.startNewSubPath(0.0f, 0.95f);
        path.quadraticTo(0.5f, -0.25f, 1.0f, 0.95f);
    } else if (name == "ls") {
        path.startNewSubPath(0.0f, 0.25f);
        path.lineTo(0.3f, 0.25f);
        path.quadraticTo(0.5f, 0.25f, 0.55f, 0.5f);
        path.quadraticTo(0.6f, 0.75f, 0.8f, 0.75f);
        path.lineTo(1.0f, 0.75f);
    } else if (name == "hs") {
        path.startNewSubPath(0.0f, 0.75f);
        path.lineTo(0.2f, 0.75f);
        path.quadraticTo(0.4f, 0.75f, 0.45f, 0.5f);
        path.quadraticTo(0.5f, 0.25f, 0.7f, 0.25f);
        path.lineTo(1.0f, 0.25f);
    } else if (name == "bell") {
        path.startNewSubPath(0.0f, 0.8f);
        path.lineTo(0.2f, 0.8f);
        path.cubicTo(0.4f, 0.8f, 0.4f, 0.1f, 0.5f, 0.1f);
        path.cubicTo(0.6f, 0.1f, 0.6f, 0.8f, 0.8f, 0.8f);
        path.lineTo(1.0f, 0.8f);
    } else if (name == "notch") {
        path.startNewSubPath(0.0f, 0.3f);
        path.lineTo(0.35f, 0.3f);
        path.quadraticTo(0.5f, 1.6f, 0.65f, 0.3f);
        path.lineTo(1.0f, 0.3f);
    } else if (name == "sine") {
        sampled(
            [](float x) { return 0.5f - 0.45f * std::sin(x * juce::MathConstants<float>::twoPi); });
    } else if (name == "triangle") {
        path.startNewSubPath(0.0f, 0.5f);
        path.lineTo(0.25f, 0.05f);
        path.lineTo(0.75f, 0.95f);
        path.lineTo(1.0f, 0.5f);
    } else if (name == "square") {
        path.startNewSubPath(0.0f, 0.95f);
        path.lineTo(0.0f, 0.05f);
        path.lineTo(0.5f, 0.05f);
        path.lineTo(0.5f, 0.95f);
        path.lineTo(1.0f, 0.95f);
        path.lineTo(1.0f, 0.05f);
    } else if (name == "s&h") {
        path.startNewSubPath(0.0f, 0.6f);
        for (const auto [x, y] : {std::pair{0.25f, 0.2f}, std::pair{0.5f, 0.85f},
                                  std::pair{0.75f, 0.4f}, std::pair{1.0f, 0.4f}}) {
            path.lineTo(x, path.getCurrentPosition().y);
            path.lineTo(x, y);
        }
    } else {
        return std::nullopt;
    }
    return path;
}

float optionWidth(const juce::String& option, bool icons) {
    return icons && iconFor(option)
               ? kIconSegmentWidth
               : juce::GlyphArrangement::getStringWidth(segmentFont(), option.toUpperCase()) +
                     2.0f * kSegmentPadding;
}

}  // namespace

void SegmentedChoice::setOptions(const juce::StringArray& options) {
    toggle_ = false;
    if (options == options_)
        return;
    options_ = options;
    repaint();
}

void SegmentedChoice::setToggle(const juce::String& label) {
    toggle_ = true;
    options_ = juce::StringArray(label);
    repaint();
}

void SegmentedChoice::setSelectedIndex(int index) {
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
}

int SegmentedChoice::getPreferredWidth() const {
    return preferredWidthFor(options_);
}

int SegmentedChoice::preferredWidthFor(const juce::StringArray& options) {
    const bool icons = allHaveIcons(options);
    float width = 2.0f * kInset;
    for (const auto& option : options)
        width += optionWidth(option, icons);
    return juce::roundToInt(width);
}

bool SegmentedChoice::allHaveIcons(const juce::StringArray& options) {
    // Icons only for a whole set: one picture among words reads as a different kind of option.
    return !options.isEmpty() &&
           std::all_of(options.begin(), options.end(),
                       [](const juce::String& option) { return iconFor(option).has_value(); });
}

juce::Rectangle<float> SegmentedChoice::segmentBounds(int index) const {
    auto area = getLocalBounds().toFloat().reduced(kInset);
    const auto scale =
        area.getWidth() / juce::jmax(1.0f, static_cast<float>(getPreferredWidth()) - 2.0f * kInset);
    for (int i = 0; i < options_.size(); ++i) {
        auto segment =
            area.removeFromLeft(optionWidth(options_[i], allHaveIcons(options_)) * scale);
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
        const bool lit = toggle_ ? selected_ == 1 : i == selected_;
        if (lit) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
            g.fillRoundedRectangle(segment, 3.0f);
        }
        g.setColour(
            ActiveTheme::getColour(lit ? ActiveTheme::DEVICE_TEXT : ActiveTheme::DEVICE_DIM));
        if (auto icon = allHaveIcons(options_) ? iconFor(options_[i]) : std::nullopt) {
            const auto box = segment.withSizeKeepingCentre(16.0f, 10.0f);
            icon->applyTransform(juce::AffineTransform::scale(box.getWidth(), box.getHeight())
                                     .translated(box.getX(), box.getY()));
            g.strokePath(*icon, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        } else {
            g.drawText(options_[i].toUpperCase(), segment, juce::Justification::centred, false);
        }
    }
}

void SegmentedChoice::mouseDown(const juce::MouseEvent& e) {
    if (toggle_) {
        selected_ = selected_ == 1 ? 0 : 1;
        repaint();
        if (onChange)
            onChange(selected_);
        return;
    }
    for (int i = 0; i < options_.size(); ++i) {
        if (segmentBounds(i).contains(e.position) && i != selected_) {
            setSelectedIndex(i);
            if (onChange)
                onChange(i);
            return;
        }
    }
}

juce::String SegmentedChoice::getTooltip() {
    static const std::map<juce::String, juce::String> kNames{
        {"lp", "Low Pass"}, {"hp", "High Pass"},       {"bp", "Band Pass"},  {"lc", "Low Cut"},
        {"hc", "High Cut"}, {"ls", "Low Shelf"},       {"hs", "High Shelf"}, {"bell", "Bell"},
        {"notch", "Notch"}, {"s&h", "Sample and Hold"}};
    const auto mouse = getMouseXYRelative().toFloat();
    for (int i = 0; i < options_.size(); ++i) {
        if (!segmentBounds(i).contains(mouse) || !allHaveIcons(options_))
            continue;
        const auto found = kNames.find(options_[i].toLowerCase());
        return found != kNames.end() ? found->second : options_[i];
    }
    return {};
}

}  // namespace magda
