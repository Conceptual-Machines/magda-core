#pragma once

namespace magica {

/**
 * @brief Centralized metrics for mixer UI components
 *
 * All fader/mixer dimensions are calculated from base values,
 * ensuring proportional scaling and consistency across components.
 */
struct MixerMetrics {
    // === Base values (tune these) ===
    float thumbHeight = 12.0f;
    float thumbWidthMultiplier = 3.0f;     // thumbWidth = thumbHeight * this (36px)
    float trackWidthMultiplier = 0.42f;    // trackWidth = thumbHeight * this (5px)
    float tickWidthMultiplier = 0.667f;    // tickWidth = thumbHeight * this (8px)
    float trackPaddingMultiplier = 0.25f;  // trackPadding = thumbHeight * this (3px)

    // === Derived fader values ===
    float thumbWidth() const {
        return thumbHeight * thumbWidthMultiplier;
    }
    float thumbRadius() const {
        return thumbHeight / 2.0f;
    }
    float trackWidth() const {
        return thumbHeight * trackWidthMultiplier;
    }
    float tickWidth() const {
        return thumbHeight * tickWidthMultiplier;
    }
    float tickHeight() const {
        return 1.0f;
    }
    float trackPadding() const {
        return thumbHeight * trackPaddingMultiplier;
    }

    // === Label dimensions ===
    float labelTextWidth = 14.0f;
    float labelTextHeight = 10.0f;
    float labelFontSize = 10.0f;

    // === Channel strip dimensions ===
    int channelWidth = 120;
    int masterWidth = 140;
    int channelPadding = 4;

    // === Fader dimensions ===
    int faderWidth = 40;
    int faderHeightRatio = 60;  // percentage of available height

    // === Meter dimensions ===
    int meterWidth = 12;

    // === Control dimensions ===
    int buttonSize = 24;
    int knobSize = 40;
    int headerHeight = 30;

    // === Spacing ===
    int controlSpacing = 4;
    int tickToFaderGap = 1;
    int tickToLabelGap = 1;
    int tickToMeterGap = 2;

    // === Singleton access ===
    static MixerMetrics& getInstance() {
        static MixerMetrics instance;
        return instance;
    }

  private:
    MixerMetrics() = default;
};

}  // namespace magica
