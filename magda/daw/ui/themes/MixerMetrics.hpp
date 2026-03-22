#pragma once

namespace magda {

/**
 * @brief Centralized metrics for mixer UI components
 *
 * All fader/mixer dimensions are calculated from base values,
 * ensuring proportional scaling and consistency across components.
 */
struct MixerMetrics {
    // === Base values (tune these) ===
    float thumbHeight = 16.0f;
    float thumbWidthMultiplier = 2.0f;     // thumbWidth = thumbHeight * this (32px)
    float trackWidthMultiplier = 0.66f;    // trackWidth = thumbHeight * this (~11px)
    float tickWidthMultiplier = 0.3f;      // tickWidth = thumbHeight * this (~5px)
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
    float labelTextWidth = 22.0f;  // Wide enough for "-inf"
    float labelTextHeight = 10.0f;
    float labelFontSize = 10.0f;

    // === Channel strip dimensions ===
    int channelWidth = 100;
    int masterWidth = 100;  // Same as channel strips (resized together)
    int channelPadding = 4;

    // === Fader dimensions ===
    int faderWidth = 40;
    int faderHeightRatio = 85;  // percentage of available height

    // === Meter dimensions ===
    int meterWidth = 16;  // Stereo L/R bars (7.5px each with 1px gap)

    // === Control dimensions ===
    int buttonSize = 18;  // Compact M/S/R buttons
    int knobSize = 32;
    int headerHeight = 30;

    // === Send area ===
    int sendAreaHeight = 60;
    static constexpr int minSendAreaHeight = 0;
    static constexpr int maxSendAreaHeight = 400;

    // === Visibility ===
    bool showRouting = true;  // Show/hide I/O routing selectors on channel strips
    bool showMonitor = true;  // Show/hide R/Monitor row on channel strips

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

}  // namespace magda
