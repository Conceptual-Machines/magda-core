#include "session/ClipLaunchQuantization.hpp"

namespace magda::clip_launch {

namespace te = tracktion;

te::LaunchQType toTracktionLaunchQType(LaunchQuantize quantize) {
    switch (quantize) {
        case LaunchQuantize::None:
            return te::LaunchQType::none;
        case LaunchQuantize::EightBars:
            return te::LaunchQType::eightBars;
        case LaunchQuantize::FourBars:
            return te::LaunchQType::fourBars;
        case LaunchQuantize::TwoBars:
            return te::LaunchQType::twoBars;
        case LaunchQuantize::OneBar:
            return te::LaunchQType::bar;
        case LaunchQuantize::HalfBar:
            return te::LaunchQType::half;
        case LaunchQuantize::QuarterBar:
            return te::LaunchQType::quarter;
        case LaunchQuantize::EighthBar:
            return te::LaunchQType::eighth;
        case LaunchQuantize::SixteenthBar:
            return te::LaunchQType::sixteenth;
    }
    return te::LaunchQType::none;
}

std::optional<te::MonotonicBeat> computeQuantizedBeat(te::Edit& edit, LaunchQuantize quantize) {
    auto qType = toTracktionLaunchQType(quantize);
    if (qType == te::LaunchQType::none)
        return std::nullopt;

    auto* ctx = edit.getCurrentPlaybackContext();
    auto syncPoint = ctx ? ctx->getSyncPoint() : std::nullopt;
    if (!syncPoint)
        return std::nullopt;

    auto quantizedBeat = te::getNext(qType, edit.tempoSequence, syncPoint->beat);
    if (quantizedBeat <= syncPoint->beat) {
        quantizedBeat = te::getNext(qType, edit.tempoSequence,
                                    syncPoint->beat + te::BeatDuration::fromBeats(0.001));
    }

    double offset = syncPoint->monotonicBeat.v.inBeats() - syncPoint->beat.inBeats();
    return te::MonotonicBeat{te::BeatPosition::fromBeats(quantizedBeat.inBeats() + offset)};
}

std::optional<double> toEditTimeSeconds(te::Edit& edit, te::MonotonicBeat monotonicBeat) {
    auto* ctx = edit.getCurrentPlaybackContext();
    auto syncPoint = ctx ? ctx->getSyncPoint() : std::nullopt;
    if (!syncPoint)
        return std::nullopt;

    double offset = syncPoint->monotonicBeat.v.inBeats() - syncPoint->beat.inBeats();
    auto editBeat = te::BeatPosition::fromBeats(monotonicBeat.v.inBeats() - offset);
    return edit.tempoSequence.beatsToTime(editBeat).inSeconds();
}

}  // namespace magda::clip_launch
