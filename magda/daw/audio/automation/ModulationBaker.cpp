#include "ModulationBaker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../core/ModulatorEngine.hpp"
#include "AutomationCurveSimplifier.hpp"

namespace magda {

bool ModulationBaker::isBakeable(const ModInfo& mod) {
    return mod.type == ModType::LFO;
}

double ModulationBaker::beatsPerCycle(SyncDivision division) {
    // Mirror of ModulatorEngine::calculateSyncRateHz's beat counts, kept in
    // exact doubles so phase math doesn't accumulate float error.
    switch (division) {
        case SyncDivision::SixteenBars:
            return 64.0;
        case SyncDivision::EightBars:
            return 32.0;
        case SyncDivision::FourBars:
            return 16.0;
        case SyncDivision::TwoBars:
            return 8.0;
        case SyncDivision::Whole:
            return 4.0;
        case SyncDivision::Half:
            return 2.0;
        case SyncDivision::Quarter:
            return 1.0;
        case SyncDivision::Eighth:
            return 0.5;
        case SyncDivision::Sixteenth:
            return 0.25;
        case SyncDivision::ThirtySecond:
            return 0.125;
        case SyncDivision::DottedHalf:
            return 3.0;
        case SyncDivision::DottedQuarter:
            return 1.5;
        case SyncDivision::DottedEighth:
            return 0.75;
        case SyncDivision::DottedSixteenth:
            return 3.0 / 8.0;
        case SyncDivision::DottedThirtySecond:
            return 3.0 / 16.0;
        case SyncDivision::TripletHalf:
            return 4.0 / 3.0;
        case SyncDivision::TripletQuarter:
            return 2.0 / 3.0;
        case SyncDivision::TripletEighth:
            return 1.0 / 3.0;
        case SyncDivision::TripletSixteenth:
            return 1.0 / 6.0;
        case SyncDivision::TripletThirtySecond:
            return 1.0 / 12.0;
    }
    return 1.0;
}

double ModulationBaker::phaseAtBeat(const ModInfo& mod, double beat, const TempoMap& tempoMap) {
    double cycles;
    if (mod.tempoSync) {
        cycles = beat / beatsPerCycle(mod.syncDivision);
    } else {
        cycles = tempoMap.beatToTime(beat) * static_cast<double>(mod.rate);
    }
    double phase = std::fmod(cycles + static_cast<double>(mod.phaseOffset), 1.0);
    if (phase < 0.0)
        phase += 1.0;
    return phase;
}

double ModulationBaker::contributionAtBeat(const std::vector<Source>& sources, double beat,
                                           const TempoMap& tempoMap) {
    double total = 0.0;
    for (const auto& source : sources) {
        if (!source.mod.enabled || !source.link.enabled || !isBakeable(source.mod))
            continue;

        const auto phase = static_cast<float>(phaseAtBeat(source.mod, beat, tempoMap));
        float v = ModulatorEngine::generateWaveformForMod(source.mod, phase);
        // The drawn curve is a level envelope; the applied output is flipped
        // (see ModInfo::invertOutput / CurveSnapshot::applyOutput).
        if (source.mod.invertOutput)
            v = 1.0f - v;

        const double offset =
            source.link.bipolar ? (static_cast<double>(v) * 2.0 - 1.0) : static_cast<double>(v);
        total += offset * static_cast<double>(source.link.amount);
    }
    return total;
}

std::vector<AutomationPoint> ModulationBaker::bake(
    const std::vector<Source>& sources, const Options& opts, const TempoMap& tempoMap,
    const std::function<double(double)>& baseValueAt) {
    if (opts.endBeat <= opts.startBeat)
        return {};

    // Step: fine enough for the fastest bakeable source's cycle.
    double step = opts.maxStepBeats;
    double minCycleBeats = std::numeric_limits<double>::max();
    bool anyBakeable = false;
    for (const auto& source : sources) {
        if (!source.mod.enabled || !source.link.enabled || !isBakeable(source.mod))
            continue;
        anyBakeable = true;

        double cycleBeats;
        if (source.mod.tempoSync) {
            cycleBeats = beatsPerCycle(source.mod.syncDivision);
        } else {
            // Approximate the cycle length in beats at the range start; the
            // sampling density doesn't need to track tempo automation.
            const double bps = tempoMap.bpmAt(opts.startBeat) / 60.0;
            const double rate = std::max(1.0e-6, static_cast<double>(source.mod.rate));
            cycleBeats = bps / rate;
        }
        minCycleBeats = std::min(minCycleBeats, cycleBeats);
        step = std::min(step, cycleBeats / std::max(1, opts.samplesPerCycle));
    }
    if (!anyBakeable)
        return {};
    // The floor guards runaway sample counts, but applied flat it starves
    // fast LFOs (a 32nd-triplet cycle kept ~10 samples instead of
    // samplesPerCycle): scale it with the shortest source cycle so the floor
    // can never push a cycle below its requested density.
    step = std::max(step,
                    std::min(opts.minStepBeats, minCycleBeats / std::max(1, opts.samplesPerCycle)));

    const auto baseAt = [&](double beat) {
        return baseValueAt ? baseValueAt(beat) : opts.fallbackBaseValue;
    };

    std::vector<AutomationCurveSimplifier::Point> raw;
    raw.reserve(static_cast<size_t>((opts.endBeat - opts.startBeat) / step) + 2);
    for (double beat = opts.startBeat; beat < opts.endBeat; beat += step) {
        const double value = baseAt(beat) + contributionAtBeat(sources, beat, tempoMap);
        raw.push_back({beat, std::clamp(value, 0.0, 1.0)});
    }
    const double endValue =
        baseAt(opts.endBeat) + contributionAtBeat(sources, opts.endBeat, tempoMap);
    raw.push_back({opts.endBeat, std::clamp(endValue, 0.0, 1.0)});

    const auto keep = AutomationCurveSimplifier::simplify(raw, opts.simplifyEpsilon);

    std::vector<AutomationPoint> points;
    points.reserve(keep.size());
    for (const auto index : keep) {
        AutomationPoint point;
        point.beatPosition = raw[index].beatPosition;
        point.value = raw[index].value;
        point.curveType = AutomationCurveType::Linear;
        points.push_back(point);
    }
    return points;
}

}  // namespace magda
