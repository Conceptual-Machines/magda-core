#include "param/ParamResolve.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>

#include "core/AutomationCurve.hpp"

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

    // The scale travels with the values, because a position is only a value
    // once something says what it is a position in.
    out.setDomain(param, spec.domain);

    const int numSamples = out.numSamples();
    const int capacity = out.segmentCapacity();
    const float offset = modulationSum(spec, sources.modulation);
    const bool stepped = magda::ParameterUtils::isStepped(spec.domain);

    // Positions, with the links added and nothing else done to them. The clamp
    // and the scale are applied where the stream is read, at the position being
    // asked about rather than at the ends of a segment, which is the only place
    // either is correct (ParamBlock.hpp says why).
    //
    // A stepped parameter holds its value across a segment: there is nothing
    // between two of its values for a ramp to pass through.
    const auto write = [&](int index, int startSample, float startNormalised, float endNormalised) {
        const float start = startNormalised + offset;
        slot[index] = ParamSegment{startSample, start, stepped ? start : endNormalised + offset};
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

    // The last thing in the list, which is where a list too long for its slot
    // has to arrive whatever it does on the way.
    const auto& lastCovered = automation[static_cast<std::size_t>(covered - 1)];
    const int finalStart = trailing > 0 ? coverEnd : std::max(lastCovered.startSample, 0);
    const float finalStartNormalised = trailing > 0 ? sources.base : lastCovered.startValue;
    const float finalEndNormalised = trailing > 0 ? sources.base : lastCovered.endValue;

    int written = 0;
    int index = 0;

    // False when the list has run out of slots. The segment being offered is
    // the last one that fits and more were coming, so what goes in that slot is
    // the rest of the list coarsened into one, and the two ways of doing that
    // are not interchangeable.
    //
    // A continuous parameter ramps from here to where the lane ends, which
    // passes through every value in between and arrives on time. A stepped one
    // cannot: it has no in-between, and a ramp written for it is flattened to
    // its own start, which would throw the destination away. It spends the slot
    // on the step the lane ends on instead, holding the value it already had
    // until that step arrives. Either way the block ends where the lane says.
    //
    // With a single slot there is nothing to spend: the step would land at its
    // own sample and leave the start of the block reading a value the parameter
    // does not have yet, so the opening value is kept and the destination is
    // what goes, which is the reading a device that asked for one value per
    // block gets anyway.
    const auto offer = [&](int startSample, float startNormalised, float endNormalised) {
        const bool more = index < total - 1;
        if (more && written == capacity - 1 && (!stepped || written > 0)) {
            if (stepped)
                write(written++, finalStart, finalStartNormalised, finalStartNormalised);
            else
                write(written++, startSample, startNormalised, finalEndNormalised);
            return false;
        }

        write(written++, startSample, startNormalised, endNormalised);
        ++index;
        return written < capacity;
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

int bakeCurve(std::span<const magda::AutomationPoint> curve, const ParamSpec& spec,
              const BlockInfo& block, std::span<ParamSegment> out) {
    if (curve.empty() || out.empty())
        return 0;

    const auto valueAt = [&curve](double beat) {
        return static_cast<float>(magda::automation::valueAtBeat(curve, beat));
    };

    const float opening = valueAt(block.startBeat);

    // One value for the block, held: the incumbent engine settles a parameter
    // at the block boundary, and a device that did not ask for more is not the
    // place to start differing from it. Also the whole answer for a block with
    // no time in it, which is what a stopped transport renders.
    if (!spec.segmentAccurate || block.numSamples <= 0 || block.endBeat <= block.startBeat) {
        out[0] = ParamSegment{0, opening, opening};
        return 1;
    }

    // Whether the run a beat falls in holds its value rather than travelling to
    // the next one. A step is not a bend that happens to be steep: a segment
    // written as a ramp would glide a device through values the drawn curve
    // never has.
    const auto holdsFrom = [&curve](double beat) {
        const auto* opener = magda::automation::segmentOpening(curve, beat);
        return opener != nullptr && opener->curveType == magda::AutomationCurveType::Step;
    };

    // Where the curve ends up over this block, and where it gets there. Both
    // are needed before the walk, because a walk that runs out of room has to
    // spend its last segment on arriving rather than on where it had got to.
    //
    // Two endings, and the difference is the block's far edge. A run that
    // travels arrives at the value the boundary has, which is the value the
    // next block opens on. A run that holds arrives at the last knot inside
    // this block: a step sitting exactly on the boundary belongs to the next
    // block, and writing its value here would pull it earlier by however long
    // the run before it lasts.
    const float ending = valueAt(block.endBeat);
    const auto boundary = std::lower_bound(
        curve.begin(), curve.end(), block.endBeat,
        [](const magda::AutomationPoint& point, double beat) { return point.beatPosition < beat; });
    const bool hasKnotInside = boundary != curve.begin();
    const int endingSample = hasKnotInside ? block.sampleForBeat((boundary - 1)->beatPosition) : 0;
    const float endingHeld = hasKnotInside ? valueAt((boundary - 1)->beatPosition) : opening;

    int written = 0;
    int startSample = 0;
    float startValue = opening;
    bool holds = holdsFrom(block.startBeat);
    bool overflowed = false;

    // Close the run in progress at @p beat and open the next one there. The
    // value at a knot is the curve's value after it, which for a step is the
    // jump and for everything else is where the previous run arrived.
    const auto closeAt = [&](double beat) {
        const auto sample = block.sampleForBeat(beat);
        const auto value = valueAt(beat);

        if (sample > startSample) {
            out[static_cast<std::size_t>(written++)] =
                ParamSegment{startSample, startValue, holds ? startValue : value};
            startSample = sample;
        }

        startValue = value;
        holds = holdsFrom(beat);
    };

    const auto inside = [&block](double beat) {
        return beat > block.startBeat && beat < block.endBeat;
    };

    // The run the block opens inside, rather than the top of the lane: an
    // unrolled clip lane is as long as the arrangement, and every knot behind
    // the playhead is one this block was never going to emit.
    const auto opener = std::lower_bound(
        curve.begin(), curve.end(), block.startBeat,
        [](const magda::AutomationPoint& point, double beat) { return point.beatPosition < beat; });
    auto index = static_cast<std::size_t>(std::distance(curve.begin(), opener));
    if (index > 0)
        --index;

    // Every knot the block contains, in beat order: each breakpoint, and the
    // apex of a hard corner, which is where that curve changes direction
    // between two breakpoints rather than at one.
    for (std::size_t i = index; i < curve.size(); ++i) {
        if (curve[i].beatPosition >= block.endBeat)
            break;
        if (written + 1 >= static_cast<int>(out.size())) {
            overflowed = true;
            break;
        }

        if (inside(curve[i].beatPosition))
            closeAt(curve[i].beatPosition);

        if (i + 1 < curve.size())
            if (const auto corner = magda::automation::hardCornerOf(curve[i], curve[i + 1])) {
                if (written + 1 >= static_cast<int>(out.size())) {
                    overflowed = true;
                    break;
                }
                if (inside(corner->beat))
                    closeAt(corner->beat);
            }
    }

    // A bake that ran out of room spends its last segment on arriving, and the
    // shape of that arrival belongs to the run the block ends in rather than to
    // the run the walk gave up in. Curve type is a property of a point, so a
    // knot the walk never reached can open a run of the other kind: a step run
    // that overflowed can end in a ramp, and a ramp that overflowed can end in
    // a step.
    //
    // Either way the run before it holds a little longer, which is the same
    // coarsening ResolvedParams makes one level down when a curve outgrows its
    // slot: the steps in between are what is lost, never the arrival.
    if (overflowed && hasKnotInside) {
        const auto sample = std::max(endingSample, startSample);
        const bool finalHolds = holdsFrom((boundary - 1)->beatPosition);

        out[static_cast<std::size_t>(written++)] =
            finalHolds ? ParamSegment{sample, endingHeld, endingHeld}
                       : ParamSegment{sample, endingHeld, ending};
    } else {
        out[static_cast<std::size_t>(written++)] =
            ParamSegment{startSample, startValue, holds ? startValue : ending};
    }

    return written;
}

namespace {

/// What a link reading modifier @p index gets. The runtime's, where there is
/// one; otherwise the model's own reading, which is what every modifier
/// published before it had an engine.
float modifierValue(const ParamTable& table, const ModRuntime* mods, int index) {
    if (index < 0 || index >= static_cast<int>(table.modifiers.size()))
        return 0.0f;

    return mods != nullptr ? mods->value(index)
                           : table.modifiers[static_cast<std::size_t>(index)].value;
}

/// The rate one modifier carries, and whether that rate is a musical division.
/// Two kinds have one; an envelope's stage lengths and a follower's time
/// constants are times rather than a frequency, and the lane that would drive
/// them does not exist.
struct ModRateLane {
    LfoRate rate;
    bool tempoSync = false;
    bool exists = false;
};

ModRateLane rateLaneOf(const ParamModifier& modifier) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            return {modifier.lfo.rate, modifier.lfo.tempoSync, true};
        case ModKind::Random:
            return {modifier.random.rate, modifier.random.tempoSync, true};
        case ModKind::Adsr:
        case ModKind::Follower:
            return {};
    }
    return {};
}

/// The rate a modifier runs at this block: what its own settings say, unless
/// something reaches its Rate parameter, which resolved earlier in the order
/// precisely so this can read it.
LfoRate rateFor(const ResolvedParams& out, const ParamModifier& modifier) {
    auto lane = rateLaneOf(modifier);
    if (!lane.exists || modifier.rate == INVALID_PARAM_ID)
        return lane.rate;

    const auto resolved = out[modifier.rate];
    if (resolved.empty())
        return lane.rate;

    // One lane, two meanings, decided by the same flag that decides what the
    // period is: a frequency when the modifier is free, and a division when it
    // is synced.
    if (lane.tempoSync)
        lane.rate.rateType = rateTypeFromLaneValue(resolved.value());
    else
        lane.rate.hz = resolved.value();

    return lane.rate;
}

void resolveOneParam(const ParamTable& table, ResolvedParams& out, const ModRuntime* mods,
                     std::span<ModContribution> links, std::span<ParamSegment> segments,
                     const BlockInfo& block, ParamId param) {
    const auto index = static_cast<std::size_t>(param);

    // A hole in a sparse device window. Nothing declared it, so nothing may be
    // published for it: no segments is the one answer a device can read as "not
    // mine", and it is the answer it already gets for an index past the end of
    // its window. A fabricated value here is indistinguishable from a real one
    // (ParamSpec::declared).
    if (!table.specs[index].declared) {
        out.setSegmentCount(param, 0);
        return;
    }

    const auto reaching = table.linksFor(param);

    std::size_t used = 0;
    for (const auto& link : reaching) {
        if (used >= links.size())
            break;

        float value = 0.0f;
        switch (link.source.kind) {
            case ParamSourceRef::Kind::Parameter:
                // Already resolved: that is what the order is for.
                value = out.sourceValue(link.source.index);
                break;

            case ParamSourceRef::Kind::Modifier:
                // Already advanced, for the same reason.
                value = modifierValue(table, mods, link.source.index);
                break;
        }

        links[used++] = ModContribution{value, link.amount, link.bipolar};
    }

    const auto baked = bakeCurve(table.curveFor(param), table.specs[index], block, segments);

    ParamSources sources;
    sources.base = table.base[index];
    sources.modulation = links.first(used);
    sources.automation =
        std::span<const ParamSegment>{segments}.first(static_cast<std::size_t>(std::max(baked, 0)));
    sources.automationEnd = block.numSamples;

    resolveParam(out, param, table.specs[index], sources);
}

}  // namespace

void resolveParams(const ParamTable& table, ResolvedParams& out, std::span<ModContribution> links,
                   std::span<ParamSegment> segments, const BlockInfo& block, ModRuntime* mods) {
    // Emptied first, and then refused. A table that does not fit is not a
    // reason to keep rendering last block's values: those were resolved for a
    // parameter set this one no longer has, and a device reading them would be
    // holding a frozen project rather than an unresolved one.
    out.beginBlock(block.numSamples);

    // Indexed by the same ParamId, so a size that disagrees is two things
    // prepared against different tables. Refused whole: half a table of
    // parameters is a project with some of its values from somewhere else.
    if (table.size() != out.size())
        return;

    // The modifier list is indexed the same way, and a runtime sized for a
    // different one holds another modifier's phase at every index. Rather than
    // refuse the whole block over it, the modifiers fall back to the values
    // the table carries, which is what they published before they could move.
    const bool runnable =
        mods != nullptr && mods->size() == static_cast<int>(table.modifiers.size());
    if (runnable)
        mods->beginBlock(block);

    // Nothing behind the modifiers, so what a link reads is the table's own
    // value rather than an index into a runtime sized for another list.
    const ModRuntime* reading = runnable ? mods : nullptr;

    for (const auto& step : table.order) {
        switch (step.kind) {
            case ParamStep::Kind::Parameter:
                if (step.index >= 0 && step.index < table.size())
                    resolveOneParam(table, out, reading, links, segments, block, step.index);
                break;

            case ParamStep::Kind::Modifier:
                if (runnable && step.index >= 0 &&
                    step.index < static_cast<int>(table.modifiers.size())) {
                    const auto& modifier = table.modifiers[static_cast<std::size_t>(step.index)];
                    mods->advance(step.index, table, rateFor(out, modifier), block);
                }
                break;
        }
    }
}

}  // namespace magda::engine
