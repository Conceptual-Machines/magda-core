#include "param/ParamTableCompiler.hpp"

#include <algorithm>
#include <queue>
#include <set>

#include "core/AutomationCurve.hpp"
#include "core/ClipLaneFlattener.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ModSources.hpp"
#include "plan/RackNesting.hpp"

namespace magda::engine {

namespace {

/// A parameter index past this is a device misreporting rather than a device
/// with a great many parameters. The table allocates a slot per index up to the
/// highest one a device declares, so without a ceiling the model would be
/// deciding how much the engine allocates.
constexpr int kMaxDeviceParamIndex = 4096;

/// The one parameter a modifier exposes: its Rate, which the model addresses
/// as ControlTarget::modParam(scope, modId, 0).
constexpr int kModRateParamIndex = 0;

/// What a macro knob is: a position between nothing and everything, with the
/// meaning of both left to whatever it is linked to.
ParamSpec macroSpec() {
    ParamSpec spec;
    spec.domain.scale = magda::ParameterScale::Linear;
    spec.domain.minValue = 0.0f;
    spec.domain.maxValue = 1.0f;
    return spec;
}

/// Which engine drives a modifier of this type.
ModKind modKindOf(magda::ModType type) {
    switch (type) {
        case magda::ModType::LFO:
            return ModKind::Lfo;
        case magda::ModType::Envelope:
            return ModKind::Adsr;
        case magda::ModType::Random:
            return ModKind::Random;
        case magda::ModType::Follower:
            return ModKind::Follower;
    }
    return ModKind::Lfo;
}

/**
 * @brief Where a modifier's phase comes from, folded from what the model says.
 *
 * The model keeps the trigger mode and the tempo-sync flag apart, and the fork
 * folds the two into one sync type (ModifierHelpers::mapSyncType). Folded the
 * same way here, so a project sounds the same in both engines:
 *
 *  - a MIDI or audio trigger is a run something restarts, whether or not it is
 *    also tempo synced;
 *  - a transport trigger, and tempo sync on its own, are both a run locked to
 *    the timeline, which is what makes a synced LFO agree with the bar;
 *  - everything else free-runs at its own frequency.
 */
ModSync modSyncOf(const magda::ModInfo& mod) {
    if (mod.triggerMode == magda::LFOTriggerMode::MIDI ||
        mod.triggerMode == magda::LFOTriggerMode::Audio)
        return ModSync::Note;

    if (mod.tempoSync || mod.triggerMode == magda::LFOTriggerMode::Transport)
        return ModSync::Transport;

    return ModSync::Free;
}

/// What the model says one LFO is.
LfoSettings lfoSettingsOf(const magda::ModInfo& mod) {
    LfoSettings settings;
    settings.wave = mod.waveform;
    settings.preset = mod.curvePreset;
    settings.sync = modSyncOf(mod);
    settings.trigger = mod.triggerMode;
    settings.tempoSync = mod.tempoSync;
    settings.rate.hz = mod.rate;
    settings.rate.rateType = mod.tempoSync ? magda::syncDivisionToTeRateOrdinal(mod.syncDivision)
                                           : static_cast<int>(magda::ModRateType::Hertz);
    settings.phaseOffset = mod.phaseOffset;
    settings.oneShot = mod.oneShot;
    settings.invertOutput = mod.invertOutput;
    settings.useLoopRegion = mod.useLoopRegion;
    settings.loopStart = mod.loopStart;
    settings.loopEnd = mod.loopEnd;

    // Which triggers this LFO listens to, and whether they gate it. Both are
    // the fork's rules read off the same model fields (applyLFOProperties):
    // a note-triggered LFO is gated by its own held notes so it reads as an
    // envelope, and an audio-triggered one sits shut between hits.
    //
    // Cross-track sidechain is the one thing the model cannot say here. It is
    // a property of the device the modifier drives rather than of the modifier
    // (PluginManager owns it in the fork), so it is left off and set by
    // whoever knows, which is the same place that will feed the triggers.
    settings.gateOnTrigger = mod.triggerMode == magda::LFOTriggerMode::MIDI;
    settings.startGated = mod.triggerMode == magda::LFOTriggerMode::Audio ||
                          (mod.triggerMode == magda::LFOTriggerMode::MIDI && !mod.running);

    return settings;
}

/**
 * @brief Where an envelope's gate comes from, folded from the trigger mode.
 *
 * Not the LFO's fold, and the difference is tempo sync. For an LFO tempo sync
 * decides what the period is and therefore whether the phase is a function of
 * the timeline, so it belongs in the fold. For an envelope it only scales the
 * stage lengths, and a tempo-synced free-running envelope is still free
 * running. The fork keeps the two apart for the same reason
 * (ModifierHelpers::mapADSRSyncType).
 */
ModSync adsrSyncOf(const magda::ModInfo& mod) {
    switch (mod.triggerMode) {
        case magda::LFOTriggerMode::Transport:
            return ModSync::Transport;
        case magda::LFOTriggerMode::MIDI:
        case magda::LFOTriggerMode::Audio:
            return ModSync::Note;
        case magda::LFOTriggerMode::Free:
            break;
    }
    return ModSync::Free;
}

/// What the model says one envelope is.
AdsrSettings adsrSettingsOf(const magda::ModInfo& mod) {
    AdsrSettings settings;
    settings.attackMs = mod.envAttackMs;
    settings.decayMs = mod.envDecayMs;
    settings.releaseMs = mod.envReleaseMs;
    settings.sustain = mod.envSustain;
    settings.attackCurve = mod.envAttackCurve;
    settings.decayCurve = mod.envDecayCurve;
    settings.releaseCurve = mod.envReleaseCurve;
    settings.sync = adsrSyncOf(mod);
    settings.trigger = mod.triggerMode;
    settings.tempoSync = mod.tempoSync;

    // One division for all three stages, because that is what the model
    // carries: the fork writes it to the ADSR's separate attack, decay and
    // release sync parameters and they always hold the same value.
    settings.rateType = mod.tempoSync ? magda::syncDivisionToTeRateOrdinal(mod.syncDivision)
                                      : static_cast<int>(magda::ModRateType::Hertz);

    // The gate rules the fork applies (applyADSRProperties), read off the same
    // model fields. An audio-triggered envelope's gate belongs to the detector
    // watching its source, and it sits shut between hits; a note-triggered one
    // is shut until the model says it is running.
    settings.startGated = mod.triggerMode == magda::LFOTriggerMode::Audio ||
                          (mod.triggerMode == magda::LFOTriggerMode::MIDI && !mod.running);

    return settings;
}

/// What the model says one random modulator is.
RandomSettings randomSettingsOf(const magda::ModInfo& mod) {
    RandomSettings settings;
    settings.type = mod.randomType == 1 ? RandomShape::Noise : RandomShape::Stepped;

    // The LFO's fold, unchanged. The fork maps the two with the same call
    // (applyRandomProperties uses mapSyncType), because both read the model's
    // one trigger mode and one tempo-sync flag.
    settings.sync = modSyncOf(mod);
    settings.trigger = mod.triggerMode;
    settings.tempoSync = mod.tempoSync;
    settings.rate.hz = mod.rate;
    settings.rate.rateType = mod.tempoSync ? magda::syncDivisionToTeRateOrdinal(mod.syncDivision)
                                           : static_cast<int>(magda::ModRateType::Hertz);
    settings.shape = mod.randomShape;
    settings.smooth = mod.randomSmooth;
    settings.stepDepth = mod.randomStepDepth;

    return settings;
}

/// What the model says one envelope follower is.
FollowerSettings followerSettingsOf(const magda::ModInfo& mod) {
    FollowerSettings settings;

    // The gain belongs before the band limits and before detection, which is
    // where the fork puts it too: its own source cache applies this and holds
    // TE's post-detection gain at unity, because a level that has already been
    // detected has no frequency content left for a filter to act on.
    settings.gainDb = mod.followerGainDb;
    settings.attackMs = mod.followerAttackMs;
    settings.holdMs = mod.followerHoldMs;
    settings.releaseMs = mod.followerReleaseMs;
    settings.highPass = mod.followerHpEnabled;
    settings.highPassHz = mod.followerHpFreq;
    settings.lowPass = mod.followerLpEnabled;
    settings.lowPassHz = mod.followerLpFreq;

    return settings;
}

/**
 * @brief What the modifier's Rate lane means, and where it starts.
 *
 * One lane with two readings, chosen by the same flag that chooses what the
 * period is: a frequency while the modifier free-runs, and a division ordinal
 * while it is synced. The model's own two descriptions of that lane, because
 * they are what a curve drawn over it was normalised against, and a second
 * copy of the numbers here would be a lane the engine read differently from
 * the editor that drew it.
 */
ParamSpec modRateSpec(const magda::ModInfo& mod) {
    return paramSpecFrom(mod.tempoSync ? magda::ParameterPresets::modRateSyncDivision()
                                       : magda::ParameterPresets::modRateHz());
}

/// Where that lane sits before anything moves it, normalised.
float modRateBase(const magda::ModInfo& mod) {
    const auto spec = modRateSpec(mod);
    const auto real =
        mod.tempoSync ? laneValueFromRateType(magda::syncDivisionToTeRateOrdinal(mod.syncDivision))
                      : mod.rate;

    return magda::ParameterUtils::realToNormalized(real, spec.domain);
}

/// A link as this pass needs it, whichever kind of source it came from.
struct PendingLink {
    ParamSourceRef source;
    std::string sourceName;
    magda::ControlTarget target;
    float amount = 0.0f;
    bool bipolar = false;
};

/// One thing in the model that owns parameters: a track, a rack, or a device.
/// Collected first and allocated second, because whether a macro is worth a
/// slot depends on what every other node's links point at.
struct Node {
    ParamKey scope;
    const magda::MacroArray* macros = nullptr;
    const magda::ModArray* mods = nullptr;
    const magda::DeviceInfo* device = nullptr;
    const magda::TrackInfo* track = nullptr;

    /// The track this scope is sidechained from, or none. What the modifiers
    /// living here listen to instead of their own track (ModSources.hpp).
    magda::TrackId sidechainSource = magda::INVALID_TRACK_ID;
};

class Builder {
  public:
    ParamTable run(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                   const magda::TrackInfo& master, std::span<const magda::AutomationLaneInfo> lanes,
                   std::span<const magda::AutomationClipInfo> clips);

  private:
    ParamId add(const ParamKey& key, const ParamSpec& spec, float base);

    void walkTrack(const magda::TrackInfo& track);
    void walkRack(const magda::RackInfo& rack, magda::TrackId trackId);
    void walkElements(const std::vector<magda::ChainElement>& elements, magda::TrackId trackId,
                      magda::RackId rackId);
    void walkFlat(const std::vector<magda::PostFxChainElement>& elements, magda::TrackId trackId,
                  ChainSegment segment);

    void collectTargets();
    void allocateMixer(const magda::TrackInfo& track);
    void allocate();
    void allocateDevice(const Node& node);
    void allocateMacros(const Node& node);
    void allocateMods(const Node& node);

    void collectLaneTargets();
    void resolveLanes();
    void flattenCurves();

    /// The clip @p id names, or null for one the lane lists and the project has
    /// lost.
    const magda::AutomationClipInfo* findClip(magda::AutomationClipId id) const;

    void resolveLinks();
    void orderAndBreakCycles();
    void flattenLinks();

    void diagnose(const std::string& what) {
        table_.diagnostics.push_back(what);
    }

    ParamTable table_;

    /// The rack instances open around the point the walk has reached. A rack's
    /// macros and modifiers are addressed by its id, so a second instance
    /// claims every address the first one has.
    RackNesting nesting_;

    std::span<const magda::AutomationLaneInfo> lanes_;
    std::span<const magda::AutomationClipInfo> clips_;
    std::vector<Node> nodes_;
    std::vector<PendingLink> pending_;

    /// Every parameter something links to, whether or not it exists. What makes
    /// a macro worth a slot: one with no links of its own that a modifier
    /// drives is a macro the project uses.
    std::set<ParamKey> targeted_;

    /// Per parameter, until the cycle pass has decided which survive.
    std::vector<std::vector<ParamLink>> perParam_;

    /// Per parameter, until the curves are flattened into the table.
    std::vector<std::vector<magda::AutomationPoint>> perParamCurve_;

    /// Per modifier, on the same terms: the cycle drawn on it, or nothing for
    /// one playing a built-in waveform.
    std::vector<std::vector<magda::CurvePointData>> modCurve_;
};

ParamId Builder::add(const ParamKey& key, const ParamSpec& spec, float base) {
    const auto existing = table_.byKey.find(key);
    if (existing != table_.byKey.end()) {
        // Two things in the model claiming one address. The plan compiler
        // refuses a duplicate OpKey for the same reason: an address that names
        // two things is an address that names neither.
        diagnose(toString(key) + ": two parameters claim this address; the second is ignored");
        return existing->second;
    }

    const auto id = static_cast<ParamId>(table_.keys.size());
    table_.keys.push_back(key);
    table_.specs.push_back(spec);
    table_.base.push_back(base);
    perParam_.emplace_back();
    perParamCurve_.emplace_back();
    table_.byKey.emplace(key, id);
    return id;
}

void Builder::allocateMacros(const Node& node) {
    if (node.macros == nullptr)
        return;

    for (std::size_t i = 0; i < node.macros->size(); ++i) {
        const auto& macro = (*node.macros)[i];

        ParamKey key = node.scope;
        key.kind = ParamKey::Kind::Macro;
        key.index = static_cast<int>(i);

        // Every scope in the model comes with sixteen macros whether or not
        // anything uses them, and a project of any size would otherwise spend
        // most of its table on knobs with nothing behind them. A macro is worth
        // a slot when it drives something or something drives it; the rest are
        // stored values the model keeps and the engine has no use for.
        //
        // A lane playing over one counts too: a macro nothing drives, driving
        // nothing, with a curve on it, is a curve the project plays.
        if (macro.links.empty() && targeted_.count(key) == 0)
            continue;

        // The macro's own value is a parameter like any other: it is stored, a
        // knob moves it, and an automation lane will play over it.
        const auto id = add(key, macroSpec(), macro.value);

        for (const auto& link : macro.links)
            pending_.push_back(PendingLink{ParamSourceRef{ParamSourceRef::Kind::Parameter, id},
                                           toString(key), link.target, link.amount, link.bipolar});
    }
}

void Builder::allocateMods(const Node& node) {
    if (node.mods == nullptr)
        return;

    for (const auto& mod : *node.mods) {
        ParamKey key = node.scope;
        key.kind = ParamKey::Kind::ModParam;
        key.modId = mod.id;
        key.index = -1;

        // The model's own reading of the modifier. What a kind with no engine
        // publishes and holds, and what a block with no runtime behind it
        // reads. The two rules that are invariants rather than implementation
        // are applied here as well: an inactive modifier outputs nothing, and
        // a curve drawn as a level is applied as its complement.
        const float output = mod.enabled ? (mod.invertOutput ? 1.0f - mod.value : mod.value) : 0.0f;

        ParamModifier modifier;
        modifier.key = key;
        modifier.value = output;
        modifier.kind = modKindOf(mod.type);
        modifier.enabled = mod.enabled;
        modifier.lfo = lfoSettingsOf(mod);
        modifier.adsr = adsrSettingsOf(mod);
        modifier.random = randomSettingsOf(mod);
        modifier.follower = followerSettingsOf(mod);

        // Which track this one listens to, if it listens at all. The same rule
        // the plan compiler emits the edge from, called rather than restated
        // (ModSources.hpp says why that matters).
        if (const auto source =
                modifierSourceTrack(mod, node.sidechainSource, node.scope.trackId)) {
            modifier.source = *source;
            modifier.tap = modTapPointOf(mod);
        }

        // Cross-track is a property of the scope rather than of the modifier,
        // which is why the settings could not say it: a modifier whose scope is
        // sidechained from elsewhere is driven by that track's detector and
        // must not be retriggered by the track it lives on. The fork keeps the
        // same flag on the same terms, set by PluginManager rather than by the
        // modifier itself.
        const bool crossTrack = node.sidechainSource != magda::INVALID_TRACK_ID &&
                                node.sidechainSource != node.scope.trackId;
        modifier.lfo.skipNativeResync = crossTrack;
        modifier.adsr.skipNativeResync = crossTrack;
        modifier.random.skipNativeResync = crossTrack;

        const auto index = static_cast<int>(table_.modifiers.size());
        table_.modifiers.push_back(modifier);

        // The drawn cycle, where there is one. Copied into the table's own
        // arena for the reason the automation curves are: an audio thread
        // cannot go looking in the model, and the model is free to change
        // underneath the block that is playing it.
        modCurve_.emplace_back();
        if (mod.waveform == magda::LFOWaveform::Custom)
            modCurve_.back() = mod.curvePoints;

        // Its Rate parameter, when something reaches it: a macro or another
        // modifier driving it, or a lane playing over it. Nothing reaches the
        // rate of almost any modifier, and a parameter each for the rest would
        // be a table mostly made of numbers that never move.
        ParamKey rateKey = key;
        rateKey.index = kModRateParamIndex;

        if (targeted_.count(rateKey) != 0)
            table_.modifiers.back().rate = add(rateKey, modRateSpec(mod), modRateBase(mod));

        // A modifier the model has switched off has no links at all rather
        // than links from a source that outputs zero. The fork creates no
        // modifier for one, so nothing is assigned and nothing contributes;
        // carrying the links instead would push every bipolar target down by
        // the link's own depth, which is a modifier doing something while
        // switched off.
        if (!mod.enabled)
            continue;

        for (const auto& link : mod.links) {
            // A link the model keeps but does not apply. Not a diagnostic: it
            // is a stored amount waiting to be switched back on.
            if (!link.enabled)
                continue;

            pending_.push_back(PendingLink{ParamSourceRef{ParamSourceRef::Kind::Modifier, index},
                                           toString(key), link.target, link.amount, link.bipolar});
        }
    }
}

void Builder::allocateDevice(const Node& node) {
    if (node.device == nullptr)
        return;

    const auto& device = *node.device;
    const auto& scope = node.scope;

    // A device's parameters are one index space: its own, plus whatever its
    // wrapper injected, which addresses the same slots.
    int highest = -1;
    const auto scan = [&](const std::vector<magda::ParameterInfo>& list) {
        for (const auto& info : list) {
            if (info.paramIndex < 0 || info.paramIndex > kMaxDeviceParamIndex) {
                diagnose(toString(scope) + ": parameter index " + std::to_string(info.paramIndex) +
                         " is out of range and is ignored");
                continue;
            }
            highest = std::max(highest, info.paramIndex);
        }
    };
    scan(device.parameters);
    scan(device.wrapperParameters);

    const auto find = [&](int index) -> const magda::ParameterInfo* {
        for (const auto* list : {&device.parameters, &device.wrapperParameters})
            for (const auto& info : *list)
                if (info.paramIndex == index)
                    return &info;
        return nullptr;
    };

    // Every index from zero, so a device reads its own parameters by the index
    // it declared them at rather than by where they landed in the table. A gap
    // is a slot nothing declared and nothing reads.
    const auto first = static_cast<ParamId>(table_.keys.size());
    int gaps = 0;
    for (int index = 0; index <= highest; ++index) {
        ParamKey key = scope;
        key.kind = ParamKey::Kind::DeviceParam;
        key.index = index;

        const auto* info = find(index);
        if (info == nullptr) {
            ++gaps;
            add(key, ParamSpec{}, 0.0f);
            continue;
        }

        // The model's own inverse, rather than a straight read of the range:
        // an external plugin's stored value and its display range are not the
        // same number, and only ParameterUtils knows which parameters those
        // are.
        const auto normalised = magda::ParameterUtils::modelToNormalizedValue(
            magda::ParameterModelValue{info->currentValue}, *info);
        add(key, paramSpecFrom(*info), normalised.value);
    }

    if (gaps > 0)
        diagnose(toString(scope) + ": parameter indices are not contiguous, so " +
                 std::to_string(gaps) + " slot(s) of the window belong to no parameter");

    if (highest >= 0)
        table_.deviceWindows.emplace(scope.device, ParamTable::DeviceWindow{first, highest + 1});
}

void Builder::walkElements(const std::vector<magda::ChainElement>& elements, magda::TrackId trackId,
                           magda::RackId rackId) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            Node node;
            node.scope.scope = ParamKey::Scope::Device;
            node.scope.trackId = trackId;
            node.scope.rackId = rackId;
            node.scope.device = DeviceKey{ChainSegment::Fx, device.id};
            node.macros = &device.macros;
            node.mods = &device.mods;
            node.device = &device;
            node.sidechainSource = sidechainSourceOf(device.sidechain);
            nodes_.push_back(node);

            // A pad rack's chains hold devices like any other rack's, and the
            // plan compiles them, so their parameters, macros and mods need
            // addresses or nothing consumes them.
            //
            // Addressed under the owning device's id, not the pad rack's. A
            // stored link to a pad device names the Drum Grid there -- see the
            // chainDevice path syncDrumGridPadPlugins and the ADSR macro links
            // are built with -- and ParamKey carries the rack id, so addressing
            // these under the pad rack's own synthetic id would put them where
            // no link can find them. The rack itself gets no node: it is
            // synthesized, so it owns no macros or modifiers to address.
            if (device.pads)
                for (const auto& pad : device.pads->chains)
                    walkElements(pad.elements, trackId, device.id);
        } else if (magda::isRack(element)) {
            walkRack(magda::getRack(element), trackId);
        }
    }
}

void Builder::walkFlat(const std::vector<magda::PostFxChainElement>& elements,
                       magda::TrackId trackId, ChainSegment segment) {
    for (const auto& element : elements) {
        Node node;
        node.scope.scope = ParamKey::Scope::Device;
        node.scope.trackId = trackId;
        node.scope.device = DeviceKey{segment, element.device.id};
        node.macros = &element.device.macros;
        node.mods = &element.device.mods;
        node.device = &element.device;
        node.sidechainSource = sidechainSourceOf(element.device.sidechain);
        nodes_.push_back(node);
    }
}

void Builder::walkRack(const magda::RackInfo& rack, magda::TrackId trackId) {
    // The same instance the plan compiler passes over. Walking it would report
    // every address in the subtree as claimed twice, with nothing naming the
    // cause.
    if (nesting_.encloses(rack.id)) {
        diagnose(nesting_.cycle(rack.id) +
                 ", its parameters and everything under it are not carried");
        return;
    }

    const RackNesting::Scope scope{nesting_, rack.id};

    Node node;
    node.scope.scope = ParamKey::Scope::Rack;
    node.scope.trackId = trackId;
    node.scope.rackId = rack.id;
    node.macros = &rack.macros;
    node.mods = &rack.mods;
    node.sidechainSource = sidechainSourceOf(rack.sidechain);
    nodes_.push_back(node);

    for (const auto& chain : rack.chains)
        walkElements(chain.elements, trackId, rack.id);
}

/// The mixer values a track has, allocated only when something reaches them: a
/// project with no automation on its faders keeps the value table's answer and
/// pays nothing for a table it would not read.
void Builder::allocateMixer(const magda::TrackInfo& track) {
    ParamKey scope;
    scope.scope = ParamKey::Scope::Track;
    scope.trackId = track.id;

    const auto fader = magda::ParameterPresets::faderVolume(-1, "Volume");
    const auto panning = magda::ParameterPresets::pan(-1, "Pan");

    // Volume and pan together or not at all. They are one op's two gains, put
    // there by one pan law, so a fader resolved from a parameter has to resolve
    // both of them: a pan lane over a table with no volume in it would have
    // nothing to pan, and a volume lane over a table with no pan in it would
    // recentre a track the moment its fader moved.
    ParamKey volume = scope;
    volume.kind = ParamKey::Kind::TrackVolume;
    ParamKey pan = scope;
    pan.kind = ParamKey::Kind::TrackPan;

    if (targeted_.count(volume) > 0 || targeted_.count(pan) > 0) {
        add(volume, paramSpecFrom(fader),
            magda::ParameterUtils::normalizedFromGain(track.volume, fader));
        add(pan, paramSpecFrom(panning),
            magda::ParameterUtils::realToNormalized(track.pan, panning));
    }

    for (std::size_t slot = 0; slot < track.sends.size(); ++slot) {
        ParamKey send = scope;
        send.kind = ParamKey::Kind::SendLevel;
        send.index = static_cast<int>(slot);
        if (targeted_.count(send) > 0)
            add(send, paramSpecFrom(fader),
                magda::ParameterUtils::normalizedFromGain(track.sends[slot].level, fader));
    }
}

void Builder::walkTrack(const magda::TrackInfo& track) {
    Node node;
    node.scope.scope = ParamKey::Scope::Track;
    node.scope.trackId = track.id;
    node.macros = &track.macros;
    node.mods = &track.mods;
    node.track = &track;
    nodes_.push_back(node);

    walkElements(track.chain.fxChainElements, track.id, INVALID_RACK_ID);
    walkFlat(track.chain.postFxChainElements, track.id, ChainSegment::PostFx);
    walkFlat(track.chain.mixerAnalysisElements, track.id, ChainSegment::MixerAnalysis);
}

void Builder::collectTargets() {
    const auto note = [&](const magda::ControlTarget& target) {
        if (const auto key = paramKeyFor(target))
            targeted_.insert(*key);
    };

    for (const auto& node : nodes_) {
        if (node.macros != nullptr)
            for (const auto& macro : *node.macros)
                for (const auto& link : macro.links)
                    note(link.target);

        if (node.mods != nullptr)
            for (const auto& mod : *node.mods)
                for (const auto& link : mod.links)
                    if (link.enabled)
                        note(link.target);
    }
}

const magda::AutomationClipInfo* Builder::findClip(magda::AutomationClipId id) const {
    for (const auto& clip : clips_)
        if (clip.id == id)
            return &clip;
    return nullptr;
}

/// Whether the model is playing this lane rather than holding it. Read mode is
/// playback; disabled, touching and writing are all the user's hand on the
/// parameter, and the base is what a parameter is worth when a hand is on it.
bool isPlaying(const magda::AutomationLaneInfo& lane) {
    return lane.authorityState == magda::AutomationAuthorityState::Reading && lane.hasData();
}

void Builder::collectLaneTargets() {
    for (const auto& lane : lanes_)
        if (isPlaying(lane))
            if (const auto key = paramKeyFor(lane.target))
                targeted_.insert(*key);
}

void Builder::resolveLanes() {
    for (const auto& lane : lanes_) {
        if (!isPlaying(lane))
            continue;

        const auto key = paramKeyFor(lane.target);
        if (!key.has_value()) {
            diagnose(std::string("a lane plays over ") + magda::toString(lane.target.kind) +
                     ", which the parameter table does not carry yet");
            continue;
        }

        const auto id = table_.find(*key);
        if (id == INVALID_PARAM_ID) {
            diagnose("a lane plays over " + toString(*key) + ", which the project does not have");
            continue;
        }

        auto& curve = perParamCurve_[static_cast<std::size_t>(id)];
        if (!curve.empty()) {
            // Two lanes over one parameter: the model allows the shape and
            // nothing says which of them wins, so neither is guessed at.
            diagnose(toString(*key) + ": two lanes play over it, and the second is ignored");
            continue;
        }

        if (lane.isAbsolute()) {
            curve = lane.absolutePoints;
            continue;
        }

        // A clip-based lane unrolled the way the model unrolls it: loops
        // repeated, gaps held at the nearest edge, overlaps decided by the
        // lane's own precedence (#1087).
        const auto getClip = [this](magda::AutomationClipId clipId) { return findClip(clipId); };
        curve = magda::flattenClipLane(lane, getClip, [&](double beat) {
            return magda::automation::laneValueAtBeat(lane, getClip, beat);
        });
    }
}

void Builder::flattenCurves() {
    table_.curveOffsets.reserve(perParamCurve_.size() + 1);
    table_.curveOffsets.push_back(0);

    for (const auto& curve : perParamCurve_) {
        table_.curvePoints.insert(table_.curvePoints.end(), curve.begin(), curve.end());
        table_.curveOffsets.push_back(static_cast<int>(table_.curvePoints.size()));
    }

    table_.modCurveOffsets.reserve(modCurve_.size() + 1);
    table_.modCurveOffsets.push_back(0);

    for (const auto& curve : modCurve_) {
        table_.modCurvePoints.insert(table_.modCurvePoints.end(), curve.begin(), curve.end());
        table_.modCurveOffsets.push_back(static_cast<int>(table_.modCurvePoints.size()));
    }
}

void Builder::allocate() {
    for (const auto& node : nodes_) {
        // Devices first within a node, so a device's window is contiguous.
        allocateDevice(node);
        if (node.track != nullptr)
            allocateMixer(*node.track);
        allocateMacros(node);
        allocateMods(node);
    }
}

void Builder::resolveLinks() {
    for (const auto& pending : pending_) {
        const auto key = paramKeyFor(pending.target);
        if (!key.has_value()) {
            // A target this table does not carry rather than a target that is
            // wrong. Both mixer values and the tempo are resolved elsewhere;
            // what is missing is the crossing, and it is named so a project
            // that relies on one is a report rather than a silence.
            diagnose(pending.sourceName + ": links to " +
                     std::string(magda::toString(pending.target.kind)) +
                     ", which the parameter table does not carry yet");
            continue;
        }

        if (key->kind == ParamKey::Kind::ModParam && key->index != kModRateParamIndex) {
            diagnose(pending.sourceName + ": links to " + toString(*key) +
                     ", and Rate is the only parameter a modifier has");
            continue;
        }

        const auto id = table_.find(*key);
        if (id == INVALID_PARAM_ID) {
            diagnose(pending.sourceName + ": links to " + toString(*key) +
                     ", which the project does not have");
            continue;
        }

        if (!table_.specs[static_cast<std::size_t>(id)].modulatable) {
            diagnose(pending.sourceName + ": links to " + toString(*key) +
                     ", which takes no modulation");
            continue;
        }

        perParam_[static_cast<std::size_t>(id)].push_back(
            ParamLink{pending.source, pending.amount, pending.bipolar});
    }
}

/// Strongly connected components of the link graph, by Tarjan, iteratively.
///
/// Iteratively because the depth is the length of a chain of macros driving
/// macros, which is a number a project decides rather than the engine, and a
/// recursion that deep is the model choosing how much stack the compiler uses.
///
/// Returns one component id per node. Two nodes share one exactly when each can
/// reach the other, which is what a cycle is.
std::vector<int> stronglyConnectedComponents(const std::vector<std::vector<int>>& edges) {
    const auto count = edges.size();

    std::vector<int> index(count, -1);
    std::vector<int> lowlink(count, 0);
    std::vector<char> onStack(count, 0);
    std::vector<int> component(count, -1);
    std::vector<int> stack;
    std::vector<std::pair<std::size_t, std::size_t>> work;

    int nextIndex = 0;
    int nextComponent = 0;

    for (std::size_t root = 0; root < count; ++root) {
        if (index[root] >= 0)
            continue;

        work.push_back({root, 0});
        while (!work.empty()) {
            auto& [node, edge] = work.back();

            if (edge == 0) {
                index[node] = nextIndex;
                lowlink[node] = nextIndex;
                ++nextIndex;
                stack.push_back(static_cast<int>(node));
                onStack[node] = 1;
            }

            if (edge < edges[node].size()) {
                const auto next = static_cast<std::size_t>(edges[node][edge]);
                ++edge;

                if (index[next] < 0) {
                    work.push_back({next, 0});
                } else if (onStack[next] != 0) {
                    lowlink[node] = std::min(lowlink[node], index[next]);
                }
                continue;
            }

            // Every edge walked: this node is finished, and it closes a
            // component when nothing under it reached back past it.
            if (lowlink[node] == index[node]) {
                while (true) {
                    const auto member = static_cast<std::size_t>(stack.back());
                    stack.pop_back();
                    onStack[member] = 0;
                    component[member] = nextComponent;
                    if (member == node)
                        break;
                }
                ++nextComponent;
            }

            const auto finished = node;
            work.pop_back();
            if (!work.empty())
                lowlink[work.back().first] =
                    std::min(lowlink[work.back().first], lowlink[finished]);
        }
    }

    return component;
}

void Builder::orderAndBreakCycles() {
    // Two kinds of node in one graph, because they depend on each other in
    // both directions: a parameter reads the modifiers linked to it, and a
    // modifier reads the Rate parameter driving it. Parameters first, then the
    // modifiers, so a node id is an index into one or the other.
    const auto params = static_cast<std::size_t>(table_.size());
    const auto mods = table_.modifiers.size();
    const auto count = params + mods;

    const auto nodeForMod = [params](std::size_t modifier) { return params + modifier; };

    // What each node reads.
    const auto forEachDependency = [&](std::size_t node, const auto& visit) {
        if (node < params) {
            for (const auto& link : perParam_[node]) {
                const auto source = static_cast<std::size_t>(link.source.index);
                switch (link.source.kind) {
                    case ParamSourceRef::Kind::Parameter:
                        if (source < params)
                            visit(source);
                        break;
                    case ParamSourceRef::Kind::Modifier:
                        if (source < mods)
                            visit(nodeForMod(source));
                        break;
                }
            }
            return;
        }

        const auto rate = table_.modifiers[node - params].rate;
        if (rate != INVALID_PARAM_ID && static_cast<std::size_t>(rate) < params)
            visit(static_cast<std::size_t>(rate));
    };

    std::vector<std::vector<int>> consumers(count);
    const auto buildConsumers = [&] {
        for (auto& list : consumers)
            list.clear();
        for (std::size_t node = 0; node < count; ++node)
            forEachDependency(node, [&](std::size_t source) {
                consumers[source].push_back(static_cast<int>(node));
            });
    };

    buildConsumers();

    // A cycle is a component with more than one node in it, or a node that
    // reads itself. Only what is inside such a component is dropped:
    // everything hanging off a cycle waits on it too, and a parameter that
    // merely reads a cycle member has done nothing wrong. Once the cycle's own
    // edges are gone its members resolve at their base, and the link that
    // reads one of them is honoured against that.
    const auto component = stronglyConnectedComponents(consumers);

    std::vector<int> componentSize(count, 0);
    for (const auto id : component)
        if (id >= 0)
            ++componentSize[static_cast<std::size_t>(id)];

    const auto insideCycle = [&](std::size_t node, std::size_t source) {
        if (component[source] != component[node])
            return false;
        return source == node || componentSize[static_cast<std::size_t>(component[node])] > 1;
    };

    const auto describe = [&](std::size_t node) {
        return node < params ? toString(table_.keys[node])
                             : toString(table_.modifiers[node - params].key);
    };

    for (std::size_t target = 0; target < params; ++target) {
        auto& links = perParam_[target];
        const auto removed = std::remove_if(links.begin(), links.end(), [&](const ParamLink& link) {
            const auto source = static_cast<std::size_t>(link.source.index);
            switch (link.source.kind) {
                case ParamSourceRef::Kind::Parameter:
                    return source < params && insideCycle(target, source);
                case ParamSourceRef::Kind::Modifier:
                    return source < mods && insideCycle(target, nodeForMod(source));
            }
            return false;
        });

        if (removed == links.end())
            continue;

        diagnose(toString(table_.keys[target]) +
                 ": part of a modulation cycle; the links inside it are dropped");
        links.erase(removed, links.end());
    }

    // A modifier whose own rate is inside the cycle stops reading it and runs
    // at what the model stored, which is the same answer one level up: the
    // parameter survives, and only the edge that made it impossible goes.
    for (std::size_t modifier = 0; modifier < mods; ++modifier) {
        auto& rate = table_.modifiers[modifier].rate;
        if (rate == INVALID_PARAM_ID || static_cast<std::size_t>(rate) >= params)
            continue;

        if (!insideCycle(nodeForMod(modifier), static_cast<std::size_t>(rate)))
            continue;

        diagnose(describe(nodeForMod(modifier)) +
                 ": its rate is part of a modulation cycle, so it runs at the stored one");
        rate = INVALID_PARAM_ID;
    }

    // With the cycles broken there is an order, and it is the same walk either
    // way: ascending node among everything that is ready, so the order is a
    // property of the model rather than of how a container iterated.
    std::vector<int> waitingOn(count, 0);
    for (std::size_t node = 0; node < count; ++node)
        forEachDependency(node, [&](std::size_t) { ++waitingOn[node]; });

    buildConsumers();

    const auto step = [&](std::size_t node) {
        return node < params
                   ? ParamStep{ParamStep::Kind::Parameter, static_cast<int>(node)}
                   : ParamStep{ParamStep::Kind::Modifier, static_cast<int>(node - params)};
    };

    std::priority_queue<int, std::vector<int>, std::greater<>> ready;
    for (std::size_t i = 0; i < count; ++i)
        if (waitingOn[i] == 0)
            ready.push(static_cast<int>(i));

    table_.order.clear();
    table_.order.reserve(count);
    while (!ready.empty()) {
        const auto node = static_cast<std::size_t>(ready.top());
        ready.pop();
        table_.order.push_back(step(node));

        for (const auto consumer : consumers[node])
            if (--waitingOn[static_cast<std::size_t>(consumer)] == 0)
                ready.push(consumer);
    }

    if (table_.order.size() == count)
        return;

    // Unreachable: every cycle was broken above, and a graph without one has a
    // topological order. Reported and completed rather than asserted, because
    // the alternative is an order missing things the block would then read as
    // resolved by nobody.
    for (std::size_t i = 0; i < count; ++i)
        if (waitingOn[i] > 0) {
            diagnose(describe(i) + ": still waits on something after the cycle pass");
            table_.order.push_back(step(i));
        }
}

void Builder::flattenLinks() {
    table_.linkOffsets.reserve(perParam_.size() + 1);
    table_.linkOffsets.push_back(0);

    for (const auto& links : perParam_) {
        table_.links.insert(table_.links.end(), links.begin(), links.end());
        table_.linkOffsets.push_back(static_cast<int>(table_.links.size()));
        table_.maxLinksPerParam = std::max(table_.maxLinksPerParam, static_cast<int>(links.size()));
    }
}

ParamTable Builder::run(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                        const magda::TrackInfo& master,
                        std::span<const magda::AutomationLaneInfo> lanes,
                        std::span<const magda::AutomationClipInfo> clips) {
    lanes_ = lanes;
    clips_ = clips;
    table_.planFingerprint = planFingerprint(plan);

    for (const auto& track : tracks)
        walkTrack(track);
    walkTrack(master);

    collectTargets();
    collectLaneTargets();
    allocate();
    resolveLanes();
    resolveLinks();
    orderAndBreakCycles();
    flattenLinks();
    flattenCurves();

    table_.layoutFingerprint = paramLayoutFingerprint(table_.keys);
    table_.modifierFingerprint = paramModifierFingerprint(table_.modifiers);

    return std::move(table_);
}

}  // namespace

ParamTable compileParamTable(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                             const magda::TrackInfo& master,
                             std::span<const magda::AutomationLaneInfo> lanes,
                             std::span<const magda::AutomationClipInfo> clips) {
    return Builder{}.run(plan, tracks, master, lanes, clips);
}

}  // namespace magda::engine
