#include "ClipOcclusion.hpp"

#include <algorithm>
#include <ranges>

namespace magda {

namespace {

constexpr double kTolBeats = 1e-9;

bool overlaps(const ClipInfo& a, const ClipInfo& b) {
    const double aStart = a.placement.startBeat;
    const double aEnd = aStart + a.placement.lengthBeats;
    const double bStart = b.placement.startBeat;
    const double bEnd = bStart + b.placement.lengthBeats;
    return bStart < aEnd - kTolBeats && aStart < bEnd - kTolBeats;
}

/// The clip this one is looking for in the lane, or nullptr.
const ClipInfo* findClip(const std::vector<ClipInfo>& trackClips, ClipId clipId) {
    for (const auto& candidate : trackClips)
        if (candidate.id == clipId)
            return &candidate;
    return nullptr;
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

bool clipSitsBelow(const ClipInfo& a, const ClipInfo& b) {
    if (a.stackOrder != b.stackOrder)
        return a.stackOrder < b.stackOrder;
    return a.id < b.id;
}

bool overlapPlaysThrough(const ClipInfo& a, const ClipInfo& b) {
    if (!a.overlapPlaysBoth && !b.overlapPlaysBoth)
        return false;
    return overlaps(a, b);
}

std::unordered_map<ClipId, AudibleSpan> computeAudibleSpans(
    const std::vector<ClipInfo>& trackClips) {
    std::unordered_map<ClipId, AudibleSpan> spans;
    spans.reserve(trackClips.size());

    std::vector<const ClipInfo*> ordered;
    ordered.reserve(trackClips.size());
    for (const auto& clip : trackClips)
        ordered.push_back(&clip);
    std::sort(ordered.begin(), ordered.end(),
              [](const ClipInfo* a, const ClipInfo* b) { return clipSitsBelow(*a, *b); });

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
            // The one switch: either clip asking makes the overlap play both,
            // and nothing else does. Ticking it on the clip that had gone
            // silent — the one you would reach for, and the only one with
            // anything to gain — used to do nothing at all (#2003).
            if (upper.overlapPlaysBoth || clip.overlapPlaysBoth)
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
        for (auto& cover : std::views::reverse(covers)) {
            if (cover.end.value >= end - kTolBeats)
                end = std::min(end, cover.start.value);
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

std::vector<BeatRange> computeBothPlayRanges(const std::vector<ClipInfo>& trackClips,
                                             ClipId clipId) {
    const ClipInfo* clip = findClip(trackClips, clipId);
    if (clip == nullptr)
        return {};

    const double start = clip->placement.startBeat;
    const double end = start + clip->placement.lengthBeats;
    if (!(end > start))
        return {};

    // Above and below alike: both clips of a pair that sounds together mark it
    // (#2003).
    std::vector<BeatRange> shared;
    for (const auto& other : trackClips) {
        if (other.id == clipId || !overlapPlaysThrough(*clip, other))
            continue;

        const double from = std::max(other.placement.startBeat, start);
        const double to = std::min(other.placement.startBeat + other.placement.lengthBeats, end);
        if (to > from + kTolBeats)
            shared.push_back({BeatPosition{from}, BeatPosition{to}});
    }

    return mergeRanges(std::move(shared));
}

std::vector<BeatRange> computeShowThroughRanges(const std::vector<ClipInfo>& trackClips,
                                                ClipId clipId) {
    const ClipInfo* clip = findClip(trackClips, clipId);
    if (clip == nullptr || !clip->enabled)
        return {};

    const double start = clip->placement.startBeat;
    const double end = start + clip->placement.lengthBeats;
    if (!(end > start))
        return {};

    std::vector<BeatRange> showing;
    for (const auto& other : trackClips) {
        if (other.id == clipId || !clipSitsBelow(other, *clip))
            continue;
        // A cover has nothing left to show: it silences what is under it.
        if (!overlapPlaysThrough(*clip, other))
            continue;

        const double from = std::max(other.placement.startBeat, start);
        const double to = std::min(other.placement.startBeat + other.placement.lengthBeats, end);
        if (to > from + kTolBeats)
            showing.push_back({BeatPosition{from}, BeatPosition{to}});
    }

    return mergeRanges(std::move(showing));
}

}  // namespace magda
