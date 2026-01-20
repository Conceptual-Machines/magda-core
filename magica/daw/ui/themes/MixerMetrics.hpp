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
    float thumbHeight = 16.0f;
    float thumbWidthMultiplier = 2.70f;    // thumbWidth = thumbHeight * this (~43px)
    float trackWidthMultiplier = 0.66f;    // trackWidth = thumbHeight * this (~11px)
    float tickWidthMultiplier = 0.78f;     // tickWidth = thumbHeight * this (~12px)
    float trackPaddingMultiplier = 0.25f;  // trackPadding = thumbHeight * this (4px)

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
    float labelTextWidth = 12.0f;
    float labelTextHeight = 10.0f;
    float labelFontSize = 10.0f;

    // === Channel strip dimensions ===
    int channelWidth = 100;
    int masterWidth = 140;
    int channelPadding = 4;

    // === Fader dimensions ===
    int faderWidth = 40;
    int faderHeightRatio = 85;  // percentage of available height

    // === Meter dimensions ===
    int meterWidth = 12;

    // === Control dimensions ===
    int buttonSize = 24;
    int knobSize = 32;
    int headerHeight = 30;

    // === Spacing ===
    int controlSpacing = 4;
    int tickToFaderGap = 0;
    int tickToLabelGap = 0;
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
