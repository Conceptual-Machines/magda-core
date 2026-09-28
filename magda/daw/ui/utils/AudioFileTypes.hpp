#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <ranges>

namespace magda {

/**
 * @brief What a drop target will accept, asked of JUCE rather than hardcoded.
 *
 * registerBasicFormats() adds CoreAudioFormat on macOS and
 * WindowsMediaAudioFormat on Windows, so the readable set is platform
 * dependent. The eight hand-written extension lists this replaces disagreed
 * with each other and with what SourcePool can actually load (#2147).
 */
inline const juce::StringArray& audioFileExtensions() {
    static const juce::StringArray extensions = [] {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::StringArray out;
        for (const auto& wildcard :
             juce::StringArray::fromTokens(formats.getWildcardForAllFormats(), ";", ""))
            out.add(wildcard.fromFirstOccurrenceOf("*", false, false).toLowerCase());
        return out;
    }();
    return extensions;
}

/** @brief Whether a path names an audio file this build can decode. */
inline constexpr struct IsAudioFile {
    bool operator()(const juce::String& path) const {
        const auto matches = [&path](const juce::String& ext) {
            return path.endsWithIgnoreCase(ext);
        };
        return std::ranges::any_of(audioFileExtensions(), matches);
    }
    bool operator()(const juce::File& file) const {
        return (*this)(file.getFullPathName());
    }
} isAudioFile;

/** @brief Whether a path names a Standard MIDI File. */
inline constexpr struct IsMidiFile {
    bool operator()(const juce::String& path) const {
        return path.endsWithIgnoreCase(".mid") || path.endsWithIgnoreCase(".midi");
    }
    bool operator()(const juce::File& file) const {
        return (*this)(file.getFullPathName());
    }
} isMidiFile;

}  // namespace magda
