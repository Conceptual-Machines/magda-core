#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <unordered_map>

namespace magda::engine {

/// Musical facts about the audio actually stored in a WAV file.
struct AudioFileMetadata {
    std::optional<double> tempo;
    std::optional<double> beats;
    std::optional<int> numerator;
    std::optional<int> denominator;
    std::optional<int> keyRoot;     // Pitch class, 0=C through 11=B.
    std::optional<int> keyQuality;  // 0=major, 1=minor.
    std::optional<bool> oneShot;
    juce::String description;
    juce::String originator;
};

constexpr std::size_t kBwavDescriptionBytes = 256;
constexpr std::size_t kBwavOriginatorBytes = 32;

/// BWF stores these in fixed byte arrays and JUCE cuts what overruns them;
/// cutting here keeps the caller's copy equal to what the file will hold.
inline juce::String clampedToBytes(juce::String text, std::size_t maxBytes) {
    while (text.getNumBytesAsUTF8() > maxBytes)
        text = text.dropLastCharacters(1);
    return text;
}

/// Build the JUCE metadata map shared by every WAV writer. ACID stores whole
/// beats and a MIDI root; the INFO keywords retain the full key name.
inline std::unordered_map<juce::String, juce::String> wavMetadataFor(
    const AudioFileMetadata& facts) {
    std::unordered_map<juce::String, juce::String> result;
    if (facts.description.isNotEmpty() || facts.originator.isNotEmpty()) {
        const auto bwav = juce::WavAudioFormat::createBWAVMetadata(
            clampedToBytes(facts.description, kBwavDescriptionBytes),
            clampedToBytes(facts.originator, kBwavOriginatorBytes), {},
            juce::Time::getCurrentTime(), 0, {});
        for (int i = 0; i < bwav.size(); ++i)
            result.emplace(bwav.getAllKeys()[i], bwav.getAllValues()[i]);
    }

    if (facts.tempo && std::isfinite(*facts.tempo) && *facts.tempo > 0.0)
        result[juce::WavAudioFormat::acidTempo] = juce::String(*facts.tempo, 6);
    // ACID has an integer beat count. For a render ending between beats,
    // leave it zero so loopInfoFrom derives the accurate span from tempo and
    // the file length instead of trusting a rounded count.
    if (facts.beats && std::isfinite(*facts.beats) && *facts.beats > 0.0 &&
        *facts.beats <= static_cast<double>(std::numeric_limits<int>::max()) &&
        std::abs(*facts.beats - std::round(*facts.beats)) < 0.0001)
        result[juce::WavAudioFormat::acidBeats] =
            juce::String(static_cast<int>(std::lround(*facts.beats)));
    if (facts.numerator && *facts.numerator > 0)
        result[juce::WavAudioFormat::acidNumerator] = juce::String(*facts.numerator);
    if (facts.denominator && *facts.denominator > 0)
        result[juce::WavAudioFormat::acidDenominator] = juce::String(*facts.denominator);
    if (facts.keyRoot && *facts.keyRoot >= 0 && *facts.keyRoot < 12) {
        static constexpr const char* names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};
        result[juce::WavAudioFormat::acidRootNote] = juce::String(60 + *facts.keyRoot);
        result[juce::WavAudioFormat::acidRootSet] = "1";
        result[juce::WavAudioFormat::riffInfoKeywords] =
            juce::String(names[*facts.keyRoot]) +
            (facts.keyQuality.value_or(0) == 1 ? " minor" : " major");
    }
    if (facts.oneShot)
        result[juce::WavAudioFormat::acidOneShot] = *facts.oneShot ? "1" : "0";
    // A flag is required when tempo is the chunk's only nonzero field; meter
    // and key alone do not claim that this audio was acidized.
    if (result.contains(juce::WavAudioFormat::acidTempo) ||
        result.contains(juce::WavAudioFormat::acidBeats) || facts.oneShot.value_or(false))
        result[juce::WavAudioFormat::acidizerFlag] = "1";
    return result;
}

}  // namespace magda::engine
