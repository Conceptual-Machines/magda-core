#pragma once

#include <cmath>

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
    float labelTextWidth = 22.0f;   // Wide enough for "-inf"
    float labelTextHeight = 12.0f;  // Box height for the dB readout numbers
    float labelFontSize = 11.0f;    // dB scale readout font

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

    // Empty space inserted just above the fader region. The horizontal handle
    // below the track header controls this value — drag down to shrink the
    // fader, drag up to grow it. Independent of sends (which auto-size to
    // their slot count).
    int faderTopInset = 0;
    static constexpr int minFaderTopInset = 0;
    static constexpr int maxFaderTopInset = 400;

    // Visibility toggles live on Config (mixerShowRouting / mixerShowMonitor /
    // mixerShowSends / mixerShowOscilloscope / mixerShowSpectrum /
    // mixerShowFxChain) — see MixerToggleRail.

    // === Spacing ===
    // Density-scaled: derived from the kBase* values by applyDensityScale().
    // Fader/knob/meter/strip dimensions above are widget sizes and stay put;
    // density only tightens or loosens the padding and gaps between them.
    int controlSpacing = 4;
    int tickToFaderGap = 0;
    int tickToLabelGap = 0;
    int tickToMeterGap = 2;

    // Base (normal-density) spacing values for applyDensityScale().
    static constexpr int kBaseChannelPadding = 4;
    static constexpr int kBaseControlSpacing = 4;
    static constexpr int kBaseTickToMeterGap = 2;

    // Recompute density-scaled spacing tokens from their base values.
    // Idempotent: always derived from kBase*, so re-applying never compounds.
    void applyDensityScale(float scale) {
        channelPadding = static_cast<int>(std::lround(kBaseChannelPadding * scale));
        controlSpacing = static_cast<int>(std::lround(kBaseControlSpacing * scale));
        tickToMeterGap = static_cast<int>(std::lround(kBaseTickToMeterGap * scale));
    }

    // === Singleton access ===
    static MixerMetrics& getInstance() {
        static MixerMetrics instance;
        return instance;
    }

  private:
    MixerMetrics() = default;
};

}  // namespace magda
