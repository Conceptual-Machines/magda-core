#include "io/SourceLoopInfo.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

namespace magda::engine {

namespace {

/// @p key's value, or nothing when the file did not write it. An empty string
/// counts as not written: a writer that emitted the key and left it blank said
/// no more than one that left it out.
std::optional<juce::String> valueOf(const juce::StringPairArray& metadata, const char* key) {
    const auto value = metadata[key];
    return value.isNotEmpty() ? std::optional<juce::String>(value) : std::nullopt;
}

std::optional<double> doubleOf(const juce::StringPairArray& metadata, const char* key) {
    if (const auto value = valueOf(metadata, key))
        return value->getDoubleValue();

    return std::nullopt;
}

std::optional<int> intOf(const juce::StringPairArray& metadata, const char* key) {
    if (const auto value = valueOf(metadata, key))
        return value->getIntValue();

    return std::nullopt;
}

}  // namespace

SourceLoopInfo loopInfoFrom(const juce::StringPairArray& metadata, double sampleRate,
                            std::int64_t lengthInSamples) {
    SourceLoopInfo info;

    // ---- The acid chunk, which is what loop libraries write ----------------

    info.numBeats = doubleOf(metadata, juce::WavAudioFormat::acidBeats);
    info.numerator = intOf(metadata, juce::WavAudioFormat::acidNumerator);
    info.denominator = intOf(metadata, juce::WavAudioFormat::acidDenominator);
    info.bpm = doubleOf(metadata, juce::WavAudioFormat::acidTempo);

    if (const auto oneShot = valueOf(metadata, juce::WavAudioFormat::acidOneShot))
        info.oneShot = *oneShot == "1";

    // The chunk carries a root note and a flag saying whether it means
    // anything. A note that has not been marked as set is not a note, which is
    // the chunk's own rule rather than a caution taken here.
    if (metadata[juce::WavAudioFormat::acidRootSet] == "1")
        info.rootNote = intOf(metadata, juce::WavAudioFormat::acidRootNote);

    // ---- The plainer keys AIFF and the fork's own files use -----------------

    if (!info.bpm)
        info.bpm = doubleOf(metadata, "tempo");

    if (!info.numBeats)
        info.numBeats = doubleOf(metadata, "beat count");

    if (const auto timeSignature = valueOf(metadata, "time signature")) {
        if (!info.denominator)
            info.denominator =
                timeSignature->upToFirstOccurrenceOf("/", false, false).getIntValue();
        if (!info.numerator)
            info.numerator = timeSignature->fromFirstOccurrenceOf("/", false, false).getIntValue();
    }

    // The smpl chunk's root, for a file with no acid chunk at all. Zero is what
    // JUCE returns for a key it does not hold, and it is also a legal note, so
    // the key's presence is what decides rather than its value.
    if (!info.rootNote)
        info.rootNote = intOf(metadata, "MidiUnityNote");

    // ---- What the file left to be worked out -------------------------------

    const auto durationSeconds = sampleRate > 0.0 && lengthInSamples > 0
                                     ? static_cast<double>(lengthInSamples) / sampleRate
                                     : 0.0;

    if (!info.bpm && info.numBeats && *info.numBeats > 0.0 && durationSeconds > 0.0)
        info.bpm = (*info.numBeats * 60.0) / durationSeconds;

    if (!info.numBeats && info.bpm && *info.bpm > 0.0 && durationSeconds > 0.0)
        info.numBeats = (durationSeconds / 60.0) * *info.bpm;

    return info;
}

}  // namespace magda::engine
