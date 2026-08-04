#include "ClipOcclusion.hpp"

#include <algorithm>

namespace magda {

namespace {

constexpr double kTolBeats = 1e-9;

/// Bottom to top. stackOrder decides; clip id breaks ties so projects saved
/// before stackOrder existed (everything 0) keep their creation order.
bool sitsBelow(const ClipInfo& a, const ClipInfo& b) {
    if (a.stackOrder != b.stackOrder)
        return a.stackOrder < b.stackOrder;
    return a.id < b.id;
}

/// An overlap between two auto-crossfade audio clips is a joint, not a cover:
/// TE fades them into each other over it (#1499). The button decides, not the
/// shape of the overlap — a clip that ends up wholly inside another still fades
/// with it rather than going silent, which is what AUTO-XFADE promises.
bool isCrossfadeJoint(const ClipInfo& lower, const ClipInfo& upper) {
    if (!lower.isAudio() || !upper.isAudio() || !lower.autoCrossfade || !upper.autoCrossfade)
        return false;

    const double lStart = lower.placement.startBeat;
    const double lEnd = lStart + lower.placement.lengthBeats;
    const double uStart = upper.placement.startBeat;
    const double uEnd = uStart + upper.placement.lengthBeats;
    return uStart < lEnd && lStart < uEnd;
}

/// Sorted, non-overlapping covers, so trimming and hole-finding can walk them
/// once. Touching ranges merge: two clips butted together cover as one.
std::vector<BeatRange> mergeRanges(std::vector<BeatRange> ranges) {
    std::sort(ranges.begin(), ranges.end(),
              [](const BeatRange& a, const BeatRange& b) { return a.start.value < b.start.value; });

    std::vector<BeatRange> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.start.value <= merged.back().end.value + kTolBeats) {
            merged.back().end.value = std::max(merged.back().end.value, range.end.value);
        } else {
            merged.push_back(range);
        }
    }
    return merged;
}

}  // namespace

std::unordered_map<ClipId, AudibleSpan> computeAudibleSpans(
    const std::vector<ClipInfo>& trackClips) {
    std::unordered_map<ClipId, AudibleSpan> spans;
    spans.reserve(trackClips.size());

    std::vector<const ClipInfo*> ordered;
    ordered.reserve(trackClips.size());
    for (const auto& clip : trackClips)
        ordered.push_back(&clip);
    std::sort(ordered.begin(), ordered.end(),
              [](const ClipInfo* a, const ClipInfo* b) { return sitsBelow(*a, *b); });

    for (size_t i = 0; i < ordered.size(); ++i) {
        const ClipInfo& clip = *ordered[i];

        AudibleSpan span;
        span.startBeat = clip.placement.startBeat;
        span.lengthBeats = clip.placement.lengthBeats;
        span.audible = span.lengthBeats > 0.0;
        if (!span.audible) {
            span.lengthBeats = 0.0;
            spans[clip.id] = span;
            continue;
        }

        const double clipStart = span.startBeat;
        const double clipEnd = span.endBeat();

        // Only the clips above this one can cover it.
        std::vector<BeatRange> covers;
        for (size_t j = i + 1; j < ordered.size(); ++j) {
            const ClipInfo& upper = *ordered[j];

            // A clip nobody can hear covers nothing: letting a hand-disabled
            // clip (#1736) occlude would silence the clip below on behalf of
            // silence.
            if (!upper.enabled)
                continue;
            if (isCrossfadeJoint(clip, upper))
                continue;
            // The clip on top decides whether it silences what it covers
            // (#2003). One owner, so unticking it on the clip you can see does
            // what it says — with either side able to veto, a clip that picked
            // the setting up from the preference kept the whole stack sounding
            // however you set the one above it.
            if (upper.overlapPlaysBoth)
                continue;

            const double uStart = upper.placement.startBeat;
            const double uEnd = uStart + upper.placement.lengthBeats;
            if (!(uEnd > uStart))
                continue;

            const double from = std::max(uStart, clipStart);
            const double to = std::min(uEnd, clipEnd);
            if (to > from + kTolBeats)
                covers.push_back({BeatPosition{from}, BeatPosition{to}});
        }

        covers = mergeRanges(std::move(covers));

        // Covers touching an edge pull that edge in — that is a shorter clip,
        // not a hole in it.
        double start = clipStart;
        double end = clipEnd;
        for (const auto& cover : covers) {
            if (cover.start.value <= start + kTolBeats)
                start = std::max(start, cover.end.value);
        }
        for (auto it = covers.rbegin(); it != covers.rend(); ++it) {
            if (it->end.value >= end - kTolBeats)
                end = std::min(end, it->start.value);
        }

        if (end <= start + kTolBeats) {
            span.audible = false;
            span.lengthBeats = 0.0;
            spans[clip.id] = span;
            continue;
        }

        span.startBeat = start;
        span.lengthBeats = end - start;

        // Whatever is left sits strictly inside the clip: a hole.
        for (const auto& cover : covers) {
            const double from = std::max(cover.start.value, start);
            const double to = std::min(cover.end.value, end);
            if (to > from + kTolBeats)
                span.silenced.push_back({BeatPosition{from}, BeatPosition{to}});
        }

        spans[clip.id] = span;
    }

    return spans;
}

std::vector<BeatRange> computeCoveringRanges(const std::vector<ClipInfo>& trackClips,
                                             ClipId clipId) {
    const ClipInfo* clip = nullptr;
    for (const auto& candidate : trackClips) {
        if (candidate.id == clipId) {
            clip = &candidate;
            break;
        }
    }
    if (clip == nullptr || !clip->enabled)
        return {};

    const double start = clip->placement.startBeat;
    const double end = start + clip->placement.lengthBeats;
    if (!(end > start))
        return {};

    std::vector<BeatRange> covering;
    for (const auto& other : trackClips) {
        if (other.id == clipId || !sitsBelow(other, *clip))
            continue;
        // Crossfade joints draw their own X, so they are not marked as covers.
        // A clip set to play through IS still marked: the mark says "there is a
        // clip under here", which is worth knowing either way.
        if (isCrossfadeJoint(other, *clip))
            continue;

        const double from = std::max(other.placement.startBeat, start);
        const double to = std::min(other.placement.startBeat + other.placement.lengthBeats, end);
        if (to > from + kTolBeats)
            covering.push_back({BeatPosition{from}, BeatPosition{to}});
    }

    return mergeRanges(std::move(covering));
}

std::vector<BeatRange> computeInteriorCrossfadeRanges(const std::vector<ClipInfo>& trackClips,
                                                      ClipId clipId) {
    const ClipInfo* clip = nullptr;
    for (const auto& candidate : trackClips) {
        if (candidate.id == clipId) {
            clip = &candidate;
            break;
        }
    }
    if (clip == nullptr)
        return {};

    const double start = clip->placement.startBeat;
    const double end = start + clip->placement.lengthBeats;
    if (!(end > start))
        return {};

    std::vector<BeatRange> interior;
    for (const auto& other : trackClips) {
        if (other.id == clipId || !isCrossfadeJoint(other, *clip))
            continue;

        const double oStart = other.placement.startBeat;
        const double oEnd = oStart + other.placement.lengthBeats;
        // Only the swallowed ones: anything reaching an edge is already drawn
        // as this clip's own fade in / fade out.
        if (oStart > start + kTolBeats && oEnd < end - kTolBeats)
            interior.push_back({BeatPosition{oStart}, BeatPosition{oEnd}});
    }

    return mergeRanges(std::move(interior));
}

}  // namespace magda
