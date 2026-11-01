// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/OscWaveIcons.hpp"

#include <BinaryData.h>

#include <utility>

#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui::osc_wave {

namespace {

const char* nameOf(Shape shape) {
    switch (shape) {
        case Shape::Sine:
            return "Sine";
        case Shape::Triangle:
            return "Triangle";
        case Shape::Saw:
            return "Saw";
        case Shape::Square:
            return "Square";
        case Shape::Noise:
            return "Noise";
    }
    return "";
}

std::unique_ptr<juce::Drawable> tinted(const juce::Drawable& icon, magda::ColourRole role) {
    auto copy = icon.createCopy();
    copy->replaceColour(juce::Colours::black, ActiveTheme::getColour(role));
    return copy;
}

}  // namespace

std::unique_ptr<juce::Drawable> loadIcon(Shape shape) {
    switch (shape) {
        case Shape::Sine:
            return juce::Drawable::createFromImageData(BinaryData::fadmodsine_svg,
                                                       BinaryData::fadmodsine_svgSize);
        case Shape::Triangle:
            return juce::Drawable::createFromImageData(BinaryData::fadmodtri_svg,
                                                       BinaryData::fadmodtri_svgSize);
        case Shape::Saw:
            return juce::Drawable::createFromImageData(BinaryData::fadmodsawup_svg,
                                                       BinaryData::fadmodsawup_svgSize);
        case Shape::Square:
            return juce::Drawable::createFromImageData(BinaryData::fadmodsquare_svg,
                                                       BinaryData::fadmodsquare_svgSize);
        case Shape::Noise:
            return juce::Drawable::createFromImageData(BinaryData::fadmodrandom_svg,
                                                       BinaryData::fadmodrandom_svgSize);
    }
    return nullptr;
}

void drawIcon(juce::Graphics& g, const juce::Drawable& icon, juce::Rectangle<int> area,
              float alpha) {
    tinted(icon, ActiveTheme::DEVICE_VALUE_TEXT)
        ->drawWithin(g, area.toFloat().withSizeKeepingCentre(kGlyphSize, kGlyphSize),
                     juce::RectanglePlacement::centred, alpha);
}

void showMenu(juce::Component& owner, juce::Rectangle<int> area, const std::vector<Shape>& shapes,
              int selected, std::function<void(int)> onPick) {
    juce::PopupMenu menu;
    for (size_t i = 0; i < shapes.size(); ++i) {
        const int index = static_cast<int>(i);
        auto icon = loadIcon(shapes[i]);
        menu.addItem(index + 1, nameOf(shapes[i]), true, index == selected,
                     icon != nullptr ? tinted(*icon, ActiveTheme::DEVICE_TEXT) : nullptr);
    }
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(owner.localAreaToGlobal(area)),
        [onPick = std::move(onPick)](int result) {
            if (result > 0 && onPick)
                onPick(result - 1);
        });
}

}  // namespace magda::daw::ui::osc_wave
