#include "param/ParamResolve.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace magda::engine {

namespace {

/// Everything the links add, before the clamp. A parameter that does not take
/// modulation adds nothing rather than dropping the links: the links are the
/// model's, and a block is not where a model edit happens.
float modulationSum(const ParamSpec& spec, std::span<const ModContribution> links) {
    if (!spec.modulatable)
        return 0.0f;

    float offset = 0.0f;
    for (const auto& link : links)
        offset += magda::ParameterUtils::modulationOffset(link.value, link.amount, link.bipolar);

    return offset;
}

}  // namespace

void resolveParam(ResolvedParams& out, int param, const ParamSpec& spec,
                  const ParamSources& sources) {
    auto* slot = out.slotFor(param);
    if (slot == nullptr)
        return;

    const int numSamples = out.numSamples();
    const int capacity = out.segmentCapacity();
    const float offset = modulationSum(spec, sources.modulation);
    const bool stepped = magda::ParameterUtils::isStepped(spec.domain);

    // The one clamp, and the one conversion out of the normalised domain the
    // lanes share into the units the device reads.
    const auto toReal = [&](float normalised) {
        return magda::ParameterUtils::normalizedToReal(
            juce::jlimit(0.0f, 1.0f, normalised + offset), spec.domain);
    };

    // A stepped parameter holds its value across a segment: there is nothing
    // between two of its values for a ramp to pass through.
    const auto write = [&](int index, int startSample, float startNormalised, float endNormalised) {
        const float startValue = toReal(startNormalised);
        slot[index] =
            ParamSegment{startSample, startValue, stepped ? startValue : toReal(endNormalised)};
    };

    const auto& automation = sources.automation;

    const int coverStart =
        automation.empty() ? 0 : std::clamp(automation.front().startSample, 0, numSamples);
    // Zero is the common case spelled shortest: an absolute lane covers the
    // whole block, and only a lane that stops inside one says where.
    const int coverEnd = automation.empty()
                             ? 0
                             : (sources.automationEnd <= 0
                                    ? numSamples
                                    : std::clamp(sources.automationEnd, coverStart, numSamples));

    int covered = 0;
    for (const auto& segment : automation) {
        if (segment.startSample >= coverEnd)
            break;
        ++covered;
    }

    const bool coversFirstSample = covered > 0 && coverStart == 0;

    // The value the block opens with, which is all a device that reads once for
    // the block ever sees, and all there is to write when nothing moves.
    const float openingValue = coversFirstSample ? automation.front().startValue : sources.base;

    if (covered == 0 || !spec.segmentAccurate) {
        write(0, 0, openingValue, openingValue);
        out.setSegmentCount(param, 1);
        return;
    }

    // The virtual list: the base before the lane, the lane, the base after it.
    // Never built anywhere, because building it would allocate; walked instead,
    // which is what the index arithmetic below is for.
    const int leading = coverStart > 0 ? 1 : 0;
    const int trailing = coverEnd < numSamples ? 1 : 0;
    const int total = leading + covered + trailing;

    // Where the block ends up, which is where a list too long for its slot has
    // to arrive anyway.
    const float finalNormalised =
        trailing > 0 ? sources.base : automation[static_cast<std::size_t>(covered - 1)].endValue;

    int written = 0;
    int index = 0;

    // True when the segment just offered was the last one that fits and more
    // were coming: it is written as a ramp to where the lane ends instead, and
    // the rest are rolled into it.
    const auto offer = [&](int startSample, float startNormalised, float endNormalised) {
        const bool more = index < total - 1;
        if (more && written == capacity - 1) {
            write(written++, startSample, startNormalised, finalNormalised);
            return false;
        }

        write(written++, startSample, startNormalised, endNormalised);
        ++index;
        return true;
    };

    bool room = true;

    if (leading > 0)
        room = offer(0, sources.base, sources.base);

    for (int i = 0; room && i < covered; ++i) {
        const auto& segment = automation[static_cast<std::size_t>(i)];
        room = offer(std::max(segment.startSample, 0), segment.startValue, segment.endValue);
    }

    if (room && trailing > 0)
        offer(coverEnd, sources.base, sources.base);

    out.setSegmentCount(param, written);
}

}  // namespace magda::engine
