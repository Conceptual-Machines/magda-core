#include "FontManager.hpp"

#include "core/Config.hpp"

namespace magda {

namespace {

float scaledFontSize(float size) {
    return size * static_cast<float>(Config::getInstance().getUIFontScale());
}

}  // namespace

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
        DBG("Failed to load Inter-Regular");
        success = false;
    } else {
        interFamily = interRegular->getName();
    }

    // Load Inter Medium
    interMedium = juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf,
                                                          BinaryData::InterMedium_ttfSize);
    if (!interMedium) {
        DBG("Failed to load Inter-Medium");
        success = false;
    }

    // Load Inter SemiBold
    interSemiBold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf,
                                                            BinaryData::InterSemiBold_ttfSize);
    if (!interSemiBold) {
        DBG("Failed to load Inter-SemiBold");
        success = false;
    }

    // Load Inter Bold
    interBold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterBold_ttf,
                                                        BinaryData::InterBold_ttfSize);
    if (!interBold) {
        DBG("Failed to load Inter-Bold");
        success = false;
    }

    // Load Microgramma D Extended Bold
    microgrammaBold =
        juce::Typeface::createSystemTypefaceFor(BinaryData::Microgramma_D_Extended_Bold_otf,
                                                BinaryData::Microgramma_D_Extended_Bold_otfSize);
    if (!microgrammaBold) {
        DBG("Failed to load Microgramma D Extended Bold");
        success = false;
    }

    // Load JetBrains Mono Regular
    jetBrainsMonoRegular = juce::Typeface::createSystemTypefaceFor(
        BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    if (!jetBrainsMonoRegular) {
        DBG("Failed to load JetBrains Mono Regular");
        success = false;
    }

    // Load Noto Sans CJK SC Regular — fallback for CJK glyphs that Inter doesn't cover.
    // Missing here is non-fatal; Chinese/Japanese/Korean text will fall back to OS
    // fonts instead (or render as tofu on systems without any CJK font installed).
    notoSansCJK = juce::Typeface::createSystemTypefaceFor(BinaryData::NotoSansCJKscRegular_otf,
                                                          BinaryData::NotoSansCJKscRegular_otfSize);
    if (notoSansCJK) {
        notoSansCJKFamily = notoSansCJK->getName();
        DBG("Noto Sans CJK loaded, family=" << notoSansCJKFamily);
    } else {
        DBG("Failed to load Noto Sans CJK - CJK glyphs will use OS fallback");
    }

    initialized = success;

    if (initialized) {
        DBG("Inter fonts loaded successfully");
    } else {
        DBG("Some Inter fonts failed to load, falling back to system fonts");
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
    jetBrainsMonoRegular = nullptr;
    notoSansCJK = nullptr;
    notoSansCJKFamily = {};
    interFamily = {};
    initialized = false;
}

juce::Font FontManager::withScriptFallbacks(juce::Font font) const {
    // Inter first (Latin/Cyrillic/Greek), then Noto Sans CJK (zh/ja/ko). This
    // keeps every supported locale legible even when the primary is a
    // user-selected system font that only covers Latin.
    juce::StringArray fallbacks;
    if (interFamily.isNotEmpty())
        fallbacks.add(interFamily);
    if (notoSansCJKFamily.isNotEmpty())
        fallbacks.add(notoSansCJKFamily);
    if (!fallbacks.isEmpty())
        font.setPreferredFallbackFamilies(fallbacks);
    return font;
}

juce::Font FontManager::getInterFont(float size, Weight weight) const {
    size = scaledFontSize(size);

    // A user-selected UI font family overrides the bundled Inter typefaces.
    // System families resolve by name and only carry plain/bold styles, so the
    // four Inter weights collapse to plain (Regular/Medium) or bold
    // (SemiBold/Bold). The CJK fallback still applies.
    const auto& family = Config::getInstance().getUIFontFamily();
    if (!family.empty()) {
        const auto style = (weight == Weight::SemiBold || weight == Weight::Bold)
                               ? juce::Font::bold
                               : juce::Font::plain;
        return withScriptFallbacks(juce::Font(juce::String(family), size, style));
    }

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
        return withScriptFallbacks(juce::Font(typeface).withHeight(size));
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

    return withScriptFallbacks(juce::Font(FALLBACK_FONT, size, style));
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
    size = scaledFontSize(size);
    if (microgrammaBold) {
        return withScriptFallbacks(juce::Font(microgrammaBold).withHeight(size));
    }

    // Fallback to monospace font if Microgramma isn't loaded
    return withScriptFallbacks(
        juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold));
}

juce::Font FontManager::getMonoFont(float size) const {
    size = scaledFontSize(size);
    if (jetBrainsMonoRegular) {
        return withScriptFallbacks(juce::Font(jetBrainsMonoRegular).withHeight(size));
    }

    return withScriptFallbacks(
        juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

}  // namespace magda
