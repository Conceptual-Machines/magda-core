#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "BinaryData.h"

namespace magda {

class FontManager {
  public:
    enum class Weight { Regular, Medium, SemiBold, Bold };

    static FontManager& getInstance();

    // Initialize fonts (call once at startup)
    bool initialize();

    // Shutdown and release fonts (call before JUCE shutdown)
    void shutdown();

    // Get Inter font with specified weight and size
    juce::Font getInterFont(float size, Weight weight = Weight::Regular) const;

    // Convenience methods for common UI fonts
    juce::Font getUIFont(float size = 14.0f) const;        // Regular
    juce::Font getUIFontMedium(float size = 14.0f) const;  // Medium
    juce::Font getUIFontBold(float size = 14.0f) const;    // Bold
    juce::Font getHeadingFont(float size = 18.0f) const;   // SemiBold
    juce::Font getButtonFont(float size = 13.0f) const;    // Medium
    juce::Font getTimeFont(float size = 16.0f) const;      // SemiBold (for time displays)

    // Get Microgramma D Extended Bold font (for MPC-style displays)
    juce::Font getMicrogrammaFont(float size = 11.0f) const;

    // Check if Inter fonts are loaded
    bool isInitialized() const {
        return initialized;
    }

  private:
    FontManager() = default;
    ~FontManager() = default;

    // Non-copyable
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    bool initialized = false;

    // Font typefaces
    juce::Typeface::Ptr interRegular;
    juce::Typeface::Ptr interMedium;
    juce::Typeface::Ptr interSemiBold;
    juce::Typeface::Ptr interBold;
    juce::Typeface::Ptr microgrammaBold;

    // Fallback system font name
    static constexpr const char* FALLBACK_FONT = "Helvetica";
};

}  // namespace magda
