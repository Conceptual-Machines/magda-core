#pragma once

#include <juce_core/juce_core.h>

#include "../../engine/AudioEngine.hpp"

/**
 * @file ExportFormats.hpp
 * @brief What the export dialog's format names mean to a render (#2955).
 */

namespace magda {

/// The Opus bitrates the dialog offers, all of them JUCE quality options.
inline constexpr int kExportBitratesKbps[] = {64, 96, 128, 160, 192, 256, 320};
inline constexpr int kDefaultExportBitrateKbps = 160;

inline bool isLossyExportFormat(const juce::String& format) {
    return format == "OPUS";
}

inline juce::String exportExtensionFor(const juce::String& format) {
    if (format == "FLAC")
        return ".flac";
    if (format == "OPUS")
        return ".opus";
    return ".wav";
}

/// FLAC holds 24 bits here; a lossy format has no depth, so the WAV default stands.
inline int exportBitDepthFor(const juce::String& format) {
    if (format == "WAV16")
        return 16;
    if (format == "WAV32")
        return 32;
    if (format == "WAV24" || format == "FLAC" || format == "OPUS")
        return 24;
    return 16;
}

inline OfflineRenderFormat exportRenderFormatFor(const juce::String& format) {
    if (format == "FLAC")
        return OfflineRenderFormat::Flac;
    if (format == "OPUS")
        return OfflineRenderFormat::Opus;
    return OfflineRenderFormat::Wav;
}

}  // namespace magda
