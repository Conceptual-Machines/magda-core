#include "FontManager.hpp"

#include <iostream>

namespace magda {

FontManager& FontManager::getInstance() {
    static FontManager instance;
    return instance;
}

bool FontManager::initialize() {
    if (initialized) {
        return true;
    }

    bool success = true;

    // Load Inter Regular
    interRegular = juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf,
                                                           BinaryData::InterRegular_ttfSize);
    if (!interRegular) {
        std::cerr << "Failed to load Inter-Regular" << std::endl;
        success = false;
    }

    // Load Inter Medium
    interMedium = juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf,
                                                          BinaryData::InterMedium_ttfSize);
    if (!interMedium) {
        std::cerr << "Failed to load Inter-Medium" << std::endl;
        success = false;
    }

    // Load Inter SemiBold
    interSemiBold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf,
                                                            BinaryData::InterSemiBold_ttfSize);
    if (!interSemiBold) {
        std::cerr << "Failed to load Inter-SemiBold" << std::endl;
        success = false;
    }

    // Load Inter Bold
    interBold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterBold_ttf,
                                                        BinaryData::InterBold_ttfSize);
    if (!interBold) {
        std::cerr << "Failed to load Inter-Bold" << std::endl;
        success = false;
    }

    // Load Microgramma D Extended Bold
    microgrammaBold =
        juce::Typeface::createSystemTypefaceFor(BinaryData::Microgramma_D_Extended_Bold_otf,
                                                BinaryData::Microgramma_D_Extended_Bold_otfSize);
    if (!microgrammaBold) {
        std::cerr << "Failed to load Microgramma D Extended Bold" << std::endl;
        success = false;
    }

    initialized = success;

    if (initialized) {
        std::cout << "✓ Inter fonts loaded successfully" << std::endl;
    } else {
        std::cerr << "⚠ Some Inter fonts failed to load, falling back to system fonts" << std::endl;
    }

    return initialized;
}

void FontManager::shutdown() {
    // Release typeface references before JUCE's leak detector runs
    interRegular = nullptr;
    interMedium = nullptr;
    interSemiBold = nullptr;
    interBold = nullptr;
    microgrammaBold = nullptr;
    initialized = false;
}

juce::Font FontManager::getInterFont(float size, Weight weight) const {
    juce::Typeface* typeface = nullptr;

    switch (weight) {
        case Weight::Regular:
            typeface = interRegular.get();
            break;
        case Weight::Medium:
            typeface = interMedium.get();
            break;
        case Weight::SemiBold:
            typeface = interSemiBold.get();
            break;
        case Weight::Bold:
            typeface = interBold.get();
            break;
    }

    if (typeface) {
        return juce::Font(typeface).withHeight(size);
    }

    // Fallback to system font
    auto style = juce::Font::plain;
    switch (weight) {
        case Weight::Bold:
            style = juce::Font::bold;
            break;
        default:
            style = juce::Font::plain;
            break;
    }

    return juce::Font(FALLBACK_FONT, size, style);
}

juce::Font FontManager::getUIFont(float size) const {
    return getInterFont(size, Weight::Regular);
}

juce::Font FontManager::getUIFontMedium(float size) const {
    return getInterFont(size, Weight::Medium);
}

juce::Font FontManager::getUIFontBold(float size) const {
    return getInterFont(size, Weight::Bold);
}

juce::Font FontManager::getHeadingFont(float size) const {
    return getInterFont(size, Weight::SemiBold);
}

juce::Font FontManager::getButtonFont(float size) const {
    return getInterFont(size, Weight::Medium);
}

juce::Font FontManager::getTimeFont(float size) const {
    return getInterFont(size, Weight::SemiBold);
}

juce::Font FontManager::getMicrogrammaFont(float size) const {
    if (microgrammaBold) {
        return juce::Font(microgrammaBold).withHeight(size);
    }

    // Fallback to monospace font if Microgramma isn't loaded
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold);
}

}  // namespace magda
