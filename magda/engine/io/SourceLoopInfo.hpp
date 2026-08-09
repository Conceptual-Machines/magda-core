#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>

/**
 * @file SourceLoopInfo.hpp
 * @brief What a file says about its own tempo, before anyone interprets it.
 *
 * The model seeds an event's interpretation from this and then owns it:
 * `AudioEvent::seedInterpretation` takes a beat count and a bpm, and never
 * overwrites what the user set. Where those two came from was Tracktion
 * (`te::LoopInfo`), and it is not an analysis at all -- the fork reads them out
 * of `juce::AudioFormatReader::metadataValues`, the acid chunk that loop
 * libraries write and that JUCE's `WavAudioFormat` already parses.
 *
 * So this is a parse, and it lives beside the reader rather than inside it for
 * the reason the reader interface gives for being as narrow as it is: what
 * decoded the file is the host's business. Taking a metadata map rather than a
 * file also means the whole thing is testable by handing it one, with no
 * fixture on a disk and no format to register.
 *
 * Every field is optional, and that is the point of it. "The file says nothing"
 * and "the file says 120" are different answers, and the model's seeding rule
 * depends on telling them apart: a value that defaulted would overwrite an
 * interpretation the user had already chosen.
 */

namespace magda::engine {

/**
 * @brief A source's own account of its musical shape.
 *
 * Unset means the file did not say, and unset is never filled in with a
 * default: the model's seeding rule leaves what the user set alone, and it can
 * only do that if it can tell a silence from a value.
 *
 * @ref bpm is the one derived field, and only when it has to be. A file that
 * wrote its tempo is believed; one that wrote only a beat count has its tempo
 * worked out from how long it is, which is what acid loops expect and what the
 * incumbent does for all of them. Preferring the written value where there is
 * one is the difference between seeding 174 and seeding 173.98.
 */
struct SourceLoopInfo {
    std::optional<double> bpm;
    std::optional<double> numBeats;
    std::optional<int> numerator;
    std::optional<int> denominator;

    /// MIDI note number the material is in, when the file says its root was
    /// set. A file carrying a root note it has not marked as valid is treated
    /// as not having one, which is the acid chunk's own rule.
    std::optional<int> rootNote;

    /// A hit rather than a loop. Nothing musical should be read off its length.
    std::optional<bool> oneShot;

    bool operator==(const SourceLoopInfo&) const = default;
};

/**
 * @brief Read @p metadata for what the file said about its tempo.
 *
 * @p sampleRate and @p lengthInSamples are what a beat count is turned into a
 * bpm with when the file gave a count and no tempo, which is the common case:
 * acid loops carry their beat count and leave the tempo to be worked out from
 * how long they are. Pass zero for either and that inference is skipped rather
 * than guessed.
 */
SourceLoopInfo loopInfoFrom(const juce::StringPairArray& metadata, double sampleRate,
                            std::int64_t lengthInSamples);

}  // namespace magda::engine
