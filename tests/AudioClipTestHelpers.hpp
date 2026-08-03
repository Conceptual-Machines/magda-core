#pragma once

#include "core/ClipInfo.hpp"
#include "core/SourcePool.hpp"

namespace magda::test {

/// Sample rate model tests pretend their sources have.
///
/// An event's anchor and loop region are stored in source samples, so a test
/// that never touches disk still needs the pool to report a rate. Seeding one
/// keeps the seconds <-> samples round trip exact instead of silently falling
/// back to kUnresolvedSourceSampleRate.
constexpr double kTestSourceSampleRate = 48000.0;

/// Give a clip a single audio event reading a (fake) file.
///
/// Mirrors what ClipManager::createAudioClipBeats builds: one event spanning
/// the clip, on a pooled source. Returns the event so the test can set its
/// interpretation.
inline AudioEvent& giveAudioEvent(ClipInfo& clip, const juce::String& filePath,
                                  double durationSeconds = 0.0,
                                  double sampleRate = kTestSourceSampleRate) {
    clip.setAudioContent();

    auto& pool = SourcePool::getInstance();
    pool.seedFactsForTesting(filePath, durationSeconds, sampleRate);

    AudioEvent event;
    event.sourceId = pool.acquire(filePath);
    auto& added = clip.audio().addEvent(std::move(event));
    clip.syncSingleEventToClipBounds();
    return added;
}

/// Set the pooled source's on-disk duration for an existing event.
///
/// Duration is a Source fact, not an event field, so a test that wants one
/// seeds the pool and re-resolves rather than writing it on the clip.
inline void setSourceDuration(ClipInfo& clip, double durationSeconds,
                              double sampleRate = kTestSourceSampleRate) {
    auto* event = clip.primaryEvent();
    if (event == nullptr)
        return;

    auto& pool = SourcePool::getInstance();
    if (event->sourceId == INVALID_SOURCE_ID) {
        // The test never named a file, so give the event one: a duration has
        // to live on a pooled Source. The path is unique per call so unrelated
        // tests never end up sharing (and overwriting) one source.
        static int syntheticSourceCount = 0;
        event->sourceId =
            pool.acquire("magda_test_source_" + juce::String(++syntheticSourceCount) + ".wav");
    }

    pool.seedFactsForTesting(event->sourceFilePath(), durationSeconds, sampleRate);
    pool.resolveFacts(event->sourceId);
    if (auto* source = pool.getMutable(event->sourceId))
        source->durationSeconds = durationSeconds;
}

/// Mutable primary audio event, created on demand.
///
/// A test that only does `clip.setAudioContent()` has an audio clip with no
/// event yet; giving it one here means every test can keep writing single
/// fields without first deciding on a source file.
inline AudioEvent& audioEvent(ClipInfo& clip) {
    jassert(clip.isAudio());
    if (clip.audio().events.empty()) {
        clip.audio().addEvent({});
        clip.syncSingleEventToClipBounds();
    }
    return *clip.primaryEvent();
}

}  // namespace magda::test
