#include "AraRegionTiming.hpp"

#include <algorithm>

#include "clip/EventPlacement.hpp"

namespace magda::daw::audio::ara {

namespace {

/** @brief The forward-file seconds @p event plays at one instant of its span. */
double sourceSecondsAt(const engine::AudioClipPlayback& clip,
                       const engine::AudioEventPlayback& event, double seconds, double beat) {
    // Read at the source's own rate, so a reading sample is a file sample.
    const double rate = event.sourceSampleRate;
    const auto how = engine::sourceReadFor(event, rate);
    return engine::sourceSecondsOfReading(
        how, engine::readingPositionAt(clip, event, seconds, beat, rate));
}

}  // namespace

AraRegionTiming regionTimingFor(const engine::AudioClipPlayback& clip,
                                const engine::AudioEventPlayback& event) {
    double first = 0.0;
    double last = 0.0;

    if (event.loopEnabled && event.loopLengthSamples > 0) {
        first = static_cast<double>(event.loopStartSamples) / event.sourceSampleRate;
        last = first + static_cast<double>(event.loopLengthSamples) / event.sourceSampleRate;
    } else {
        const auto& span = event.span;
        const double a = sourceSecondsAt(clip, event, span.seconds.start, span.beats.start);
        const double b = sourceSecondsAt(clip, event, span.seconds.end, span.beats.end);
        first = std::min(a, b);
        last = std::max(a, b);
    }

    const double fileEnd = event.sourceDurationSeconds > 0.0 ? event.sourceDurationSeconds : last;
    first = std::clamp(first, 0.0, fileEnd);
    last = std::clamp(last, first, fileEnd);

    return {.startInModification = first,
            .durationInModification = last - first,
            .startInPlayback = event.span.seconds.start,
            .durationInPlayback = last - first};
}

}  // namespace magda::daw::audio::ara
