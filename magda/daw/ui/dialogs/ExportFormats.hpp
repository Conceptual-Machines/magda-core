#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "../../engine/AudioEngine.hpp"

/**
 * @file ExportFormats.hpp
 * @brief What the export dialog's format names mean to a render (#2955).
 */

namespace magda {

/// The Opus bitrates the dialog offers, all of them JUCE quality options.
inline constexpr int kExportBitratesKbps[] = {64, 96, 128, 160, 192, 256, 320};
inline constexpr int kDefaultExportBitrateKbps = 160;

/// The MP3 choices: LAME VBR levels, then CBR bitrates. The bitrate combo's
/// item id is the kbps for CBR and kMp3VbrItemIdBase plus the level for VBR.
inline constexpr int kMp3VbrLevels[] = {0, 2, 4};
inline constexpr int kMp3CbrBitratesKbps[] = {128, 192, 256, 320};
inline constexpr int kMp3VbrItemIdBase = 1000;
inline constexpr int kDefaultMp3ItemId = kMp3VbrItemIdBase + 2;

/// What an MP3 bitrate-combo item asks the encoder for.
struct Mp3ExportQuality {
    int bitrateKbps = 0;
    std::optional<int> vbrQuality;
};

inline Mp3ExportQuality mp3QualityForItemId(int itemId) {
    if (itemId >= kMp3VbrItemIdBase)
        return {.vbrQuality = itemId - kMp3VbrItemIdBase};
    return {.bitrateKbps = itemId};
}

inline bool isLossyExportFormat(const juce::String& format) {
    return format == "OPUS" || format == "MP3";
}

inline juce::String exportExtensionFor(const juce::String& format) {
    if (format == "FLAC")
        return ".flac";
    if (format == "OPUS")
        return ".opus";
    if (format == "MP3")
        return ".mp3";
    return ".wav";
}

/// The depths a format is offered at: 32 is float, which FLAC cannot hold, and
/// a lossy format has none.
inline std::vector<int> exportBitDepthsFor(const juce::String& format) {
    if (format == "FLAC")
        return {16, 24};
    if (isLossyExportFormat(format))
        return {};
    return {16, 24, 32};
}

/// A saved format name as the dialog now spells it. Before #3034 the WAV depth
/// was part of the name ("WAV16", "WAV24", "WAV32").
inline juce::String normaliseExportFormat(const juce::String& saved) {
    if (saved == "FLAC" || saved == "OPUS" || saved == "MP3")
        return saved;
    return "WAV";
}

inline OfflineRenderFormat exportRenderFormatFor(const juce::String& format) {
    if (format == "FLAC")
        return OfflineRenderFormat::Flac;
    if (format == "OPUS")
        return OfflineRenderFormat::Opus;
    if (format == "MP3")
        return OfflineRenderFormat::Mp3;
    return OfflineRenderFormat::Wav;
}

}  // namespace magda
