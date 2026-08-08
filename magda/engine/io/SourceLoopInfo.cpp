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

/// @p key's value when it is a number worth having, and nothing otherwise.
///
/// Zero counts as nothing, and that is what makes this usable against a real
/// file rather than only against a hand-built map. JUCE writes every acid field
/// whenever a file has an acid chunk at all
/// (`WavAudioFormat::AcidChunk::addToMetadata`), zeros included, so a loop that
/// carries a beat count and no tempo arrives with `acidTempo` set to "0". Read
/// as a value that would engage the field and block the beat count below from
/// ever being turned into one.
std::optional<double> positiveDoubleOf(const juce::StringPairArray& metadata, const char* key) {
    if (const auto value = valueOf(metadata, key))
        if (const auto number = value->getDoubleValue(); number > 0.0)
            return number;

    return std::nullopt;
}

std::optional<int> positiveIntOf(const juce::StringPairArray& metadata, const char* key) {
    if (const auto value = valueOf(metadata, key))
        if (const auto number = value->getIntValue(); number > 0)
            return number;

    return std::nullopt;
}

/// @p key's value whatever it is, for the one field that has something else
/// saying whether it means anything.
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

    info.numBeats = positiveDoubleOf(metadata, juce::WavAudioFormat::acidBeats);
    info.numerator = positiveIntOf(metadata, juce::WavAudioFormat::acidNumerator);
    info.denominator = positiveIntOf(metadata, juce::WavAudioFormat::acidDenominator);
    info.bpm = positiveDoubleOf(metadata, juce::WavAudioFormat::acidTempo);

    if (const auto oneShot = valueOf(metadata, juce::WavAudioFormat::acidOneShot))
        info.oneShot = *oneShot == "1";

    // The chunk carries a root note and a flag saying whether it means
    // anything. A note that has not been marked as set is not a note, which is
    // the chunk's own rule rather than a caution taken here -- and the flag is
    // the whole of the rule, so a marked-as-set zero is note 0 rather than a
    // silence. Raw here for that reason, where every other field filters.
    if (metadata[juce::WavAudioFormat::acidRootSet] == "1")
        info.rootNote = intOf(metadata, juce::WavAudioFormat::acidRootNote);

    // ---- The plainer keys AIFF and the fork's own files use -----------------

    if (!info.bpm)
        info.bpm = positiveDoubleOf(metadata, "tempo");

    if (!info.numBeats)
        info.numBeats = positiveDoubleOf(metadata, "beat count");

    // Numerator before the slash, which is how a time signature is written and
    // NOT how the fork reads one: tracktion_LoopInfo.cpp assigns the left side
    // to the denominator and the right to the numerator, so a 6/8 file becomes
    // 8/6 there. Diverging deliberately -- none of these fields reaches the
    // audio, so nothing in the null-diff corpus moves for it, and reproducing a
    // swap would only spread it.
    if (const auto timeSignature = valueOf(metadata, "time signature")) {
        if (!info.numerator)
            info.numerator = timeSignature->upToFirstOccurrenceOf("/", false, false).getIntValue();
        if (!info.denominator)
            info.denominator =
                timeSignature->fromFirstOccurrenceOf("/", false, false).getIntValue();
    }

    // The smpl chunk's root, for a file with no acid chunk at all. Note zero
    // reads as absent, which is the fork's own treatment of this key.
    if (!info.rootNote)
        info.rootNote = positiveIntOf(metadata, "MidiUnityNote");

    // Not read: the "key signature" string, which the fork also turns into a
    // root note. That needs a note-name parser the engine has not got, and what
    // the model seeds from here is the tempo and the beat count.

    // ---- What the file left to be worked out -------------------------------

    const auto durationSeconds = sampleRate > 0.0 && lengthInSamples > 0
                                     ? static_cast<double>(lengthInSamples) / sampleRate
                                     : 0.0;

    // Beats over minutes. The fork's beat-count branch writes
    // `beats / duration / 60` here, which is a genuine bug in it -- four beats
    // over two seconds seeds 0.03 bpm rather than 120 -- and one worth not
    // reproducing: a tempo that wrong is a clip that will not stretch to any
    // sensible length.
    if (!info.bpm && info.numBeats && durationSeconds > 0.0)
        info.bpm = (*info.numBeats * 60.0) / durationSeconds;

    if (!info.numBeats && info.bpm && durationSeconds > 0.0)
        info.numBeats = (durationSeconds / 60.0) * *info.bpm;

    return info;
}

}  // namespace magda::engine
