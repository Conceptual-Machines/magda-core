#pragma once

#include <functional>
#include <vector>

#include "../../core/AutomationInfo.hpp"
#include "../../core/ModInfo.hpp"
#include "../../core/TempoMap.hpp"

namespace magda {

/**
 * @brief Offline "bake modulation to automation" sampler (issue #162).
 *
 * Samples the summed contribution of a parameter's LFO mod links over a beat
 * range and reduces the result to a sparse set of AutomationPoints via
 * Ramer-Douglas-Peucker simplification. Pure: no singletons, no engine state.
 *
 * Only LFO modulators have a phase that is deterministic at a given timeline
 * position (tempo-synced: locked to beats; Hz: locked to edit time, matching
 * TE's transport-sync path). Random needs the audio-thread RNG, Follower needs
 * rendered audio, and Envelope depends on gate events, so none of those can be
 * evaluated offline. isBakeable() encodes that rule.
 *
 * The per-link contribution formula matches both the live TE assignment
 * mapping (ModifierHelpers.hpp::mapLinkAssignment) and the UI preview math
 * (ParamLinkResolver::computeTotalModModulation):
 *   contribution = (bipolar ? v * 2 - 1 : v) * amount
 */
class ModulationBaker {
  public:
    struct Source {
        ModInfo mod;   // Modulator settings (copied; runtime fields ignored)
        ModLink link;  // Link to the target parameter (amount / bipolar)
    };

    struct Options {
        double startBeat = 0.0;
        double endBeat = 0.0;
        double fallbackBaseValue = 0.5;  // Base when baseValueAt is null
        double simplifyEpsilon = 0.005;  // RDP tolerance, normalized units
        int samplesPerCycle = 64;        // Sampling density per LFO cycle
        double minStepBeats = 1.0 / 128.0;
        double maxStepBeats = 0.25;
    };

    /** True if this modulator's output can be reproduced offline. */
    static bool isBakeable(const ModInfo& mod);

    /** Beats per LFO cycle for a sync division (Quarter = 1 beat). */
    static double beatsPerCycle(SyncDivision division);

    /**
     * Deterministic LFO phase at a beat. Tempo-synced rates divide the beat
     * position by the division's cycle length; Hz rates convert the beat to
     * edit seconds through the tempo map (correct under tempo automation).
     */
    static double phaseAtBeat(const ModInfo& mod, double beat, const TempoMap& tempoMap);

    /** Summed normalized contribution of all enabled bakeable sources. */
    static double contributionAtBeat(const std::vector<Source>& sources, double beat,
                                     const TempoMap& tempoMap);

    /**
     * Sample clamp(base + contribution) over [startBeat, endBeat] and thin
     * the result. baseValueAt (nullable) supplies the underlying automation
     * value at a beat; when null, fallbackBaseValue is used throughout.
     * Returned points carry no ids (INVALID_AUTOMATION_POINT_ID) and Linear
     * curve type; the range endpoints are always included. Returns an empty
     * vector when the range is empty or no source is bakeable.
     */
    static std::vector<AutomationPoint> bake(const std::vector<Source>& sources,
                                             const Options& opts, const TempoMap& tempoMap,
                                             const std::function<double(double)>& baseValueAt = {});
};

}  // namespace magda
