#include "ClipOcclusion.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>

#include "RangesHelpers.hpp"

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
    const auto hasId = [clipId](const ClipInfo& candidate) { return candidate.id == clipId; };
    const auto found = std::ranges::find_if(trackClips, hasId);
    return found == trackClips.end() ? nullptr : &*found;
}

const ClipInfo* addressOf(const ClipInfo& clip) {
    return &clip;
}

bool stacksBelow(const ClipInfo* a, const ClipInfo* b) {
    return clipSitsBelow(*a, *b);
}

double rangeStart(const BeatRange& range) {
    return range.start.value;
}

bool isNonEmptyRange(const BeatRange& range) {
    return range.end.value > range.start.value + kTolBeats;
}

/// Another clip's span clamped into [start, end): what the pair share.
BeatRange overlapRange(const ClipInfo& other, double start, double end) {
    const auto& placement = other.placement;
    return {BeatPosition{std::max(placement.startBeat, start)},
            BeatPosition{std::min(placement.startBeat + placement.lengthBeats, end)}};
}

/// Sorted, non-overlapping covers, so trimming and hole-finding can walk them
/// once. Touching ranges merge: two clips butted together cover as one.
std::vector<BeatRange> mergeRanges(std::vector<BeatRange> ranges) {
    std::ranges::sort(ranges, {}, rangeStart);

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

    auto ordered =
        trackClips | std::views::transform(addressOf) | toStd<std::vector<const ClipInfo*>>();
    std::ranges::sort(ordered, stacksBelow);

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

        const auto silencesThisClip = [&clip](const ClipInfo* upper) {
            // A clip nobody can hear covers nothing: letting a hand-disabled
            // clip (#1736) occlude would silence the clip below on behalf of
            // silence.
            if (!upper->enabled)
                return false;
            // The one switch: either clip asking makes the overlap play both,
            // and nothing else does. Ticking it on the clip that had gone
            // silent — the one you would reach for, and the only one with
            // anything to gain — used to do nothing at all (#2003).
            if (upper->overlapPlaysBoth || clip.overlapPlaysBoth)
                return false;
            return upper->placement.lengthBeats > 0.0;
        };
        const auto clampedToClip = [&](const ClipInfo* upper) {
            return overlapRange(*upper, clipStart, clipEnd);
        };

        // Only the clips above this one can cover it.
        auto covers = ordered | std::views::drop(static_cast<std::ptrdiff_t>(i) + 1) |
                      std::views::filter(silencesThisClip) | std::views::transform(clampedToClip) |
                      std::views::filter(isNonEmptyRange) | toStd<std::vector<BeatRange>>();
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
    const auto soundsWithClip = [&](const ClipInfo& other) {
        return other.id != clipId && overlapPlaysThrough(*clip, other);
    };
    const auto clampedToClip = [&](const ClipInfo& other) {
        return overlapRange(other, start, end);
    };

    return mergeRanges(trackClips | std::views::filter(soundsWithClip) |
                       std::views::transform(clampedToClip) | std::views::filter(isNonEmptyRange) |
                       toStd<std::vector<BeatRange>>());
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

    const auto showsThroughClip = [&](const ClipInfo& other) {
        // A cover has nothing left to show: it silences what is under it.
        return other.id != clipId && clipSitsBelow(other, *clip) &&
               overlapPlaysThrough(*clip, other);
    };
    const auto clampedToClip = [&](const ClipInfo& other) {
        return overlapRange(other, start, end);
    };

    return mergeRanges(trackClips | std::views::filter(showsThroughClip) |
                       std::views::transform(clampedToClip) | std::views::filter(isNonEmptyRange) |
                       toStd<std::vector<BeatRange>>());
}

}  // namespace magda
