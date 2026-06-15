#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>

#include "../../core/ModInfo.hpp"
#include "modifiers/CurveSnapshot.hpp"

namespace magda {

namespace te = tracktion;

struct ModifierAssignmentMapping {
    float value = 0.0f;
    float offset = 0.0f;
};

inline ModifierAssignmentMapping mapLinkAssignment(float amount, bool bipolar) {
    return {bipolar ? amount * 2.0f : amount, bipolar ? -amount : 0.0f};
}

template <typename Link>
inline te::AutomatableParameter::ModifierAssignment::Ptr addLinkModifier(
    te::AutomatableParameter& param, te::AutomatableParameter::ModifierSource& source,
    const Link& link) {
    const auto mapping = mapLinkAssignment(link.amount, link.bipolar);
    return param.addModifier(source, mapping.value, mapping.offset);
}

inline float mapWaveform(LFOWaveform waveform) {
    switch (waveform) {
        case LFOWaveform::Sine:
            return 0.0f;
        case LFOWaveform::Triangle:
            return 1.0f;
        case LFOWaveform::Saw:
            return 2.0f;
        case LFOWaveform::ReverseSaw:
            return 3.0f;
        case LFOWaveform::Square:
            return 4.0f;
        case LFOWaveform::Custom:
            return 0.0f;
    }
    return 0.0f;
}

inline float mapSyncDivision(SyncDivision div) {
    using RT = te::ModifierCommon::RateType;
    static const std::unordered_map<SyncDivision, RT> mapping = {
        {SyncDivision::SixteenBars, RT::sixteenBars},
        {SyncDivision::EightBars, RT::eightBars},
        {SyncDivision::FourBars, RT::fourBars},
        {SyncDivision::TwoBars, RT::twoBars},
        {SyncDivision::Whole, RT::bar},
        {SyncDivision::Half, RT::half},
        {SyncDivision::Quarter, RT::quarter},
        {SyncDivision::Eighth, RT::eighth},
        {SyncDivision::Sixteenth, RT::sixteenth},
        {SyncDivision::ThirtySecond, RT::thirtySecond},
        {SyncDivision::DottedHalf, RT::halfD},
        {SyncDivision::DottedQuarter, RT::quarterD},
        {SyncDivision::DottedEighth, RT::eighthD},
        {SyncDivision::DottedSixteenth, RT::sixteenthD},
        {SyncDivision::DottedThirtySecond, RT::thirtySecondD},
        {SyncDivision::TripletHalf, RT::halfT},
        {SyncDivision::TripletQuarter, RT::quarterT},
        {SyncDivision::TripletEighth, RT::eighthT},
        {SyncDivision::TripletSixteenth, RT::sixteenthT},
        {SyncDivision::TripletThirtySecond, RT::thirtySecondT},
    };
    auto it = mapping.find(div);
    return static_cast<float>(it != mapping.end() ? it->second : RT::quarter);
}

inline juce::String formatModCurveTypesForLog(const ModInfo& modInfo) {
    juce::String result;
    for (size_t i = 0; i < modInfo.curvePoints.size(); ++i) {
        if (i > 0)
            result += ",";
        result += juce::String(static_cast<int>(i));
        result += ":";
        result += juce::String(modInfo.curvePoints[i].curveType);
    }
    return result;
}

inline juce::String formatSnapshotCurveTypesForLog(const CurveSnapshot& snapshot) {
    juce::String result;
    for (int i = 0; i < snapshot.count; ++i) {
        if (i > 0)
            result += ",";
        result += juce::String(i);
        result += ":";
        result += juce::String(snapshot.points[static_cast<size_t>(i)].curveType);
    }
    return result;
}

/**
 * @brief Map MAGDA trigger/sync settings to TE syncType
 *
 * TE syncType: 0=free (Hz rate), 1=transport (tempo-synced), 2=note (MIDI retrigger)
 * Note mode can use either Hz rate (rateType=hertz) or musical divisions
 * (rateType=bar/quarter/etc.) depending on whether tempoSync is enabled.
 */
inline float mapSyncType(const ModInfo& modInfo) {
    // MIDI and Audio triggers → TE note mode (2): resets phase on triggerNoteOn()
    // Both are gate-triggered — the only difference is the trigger source.
    if (modInfo.triggerMode == LFOTriggerMode::MIDI || modInfo.triggerMode == LFOTriggerMode::Audio)
        return 2.0f;
    // Transport trigger or tempo sync both use transport mode (1)
    if (modInfo.tempoSync || modInfo.triggerMode == LFOTriggerMode::Transport)
        return 1.0f;
    // Free running in Hz
    return 0.0f;
}

inline void applyLFOProperties(te::LFOModifier* lfo, const ModInfo& modInfo,
                               CurveSnapshotHolder* holder = nullptr) {
    float syncType = mapSyncType(modInfo);

    // rateType determines Hz vs musical divisions in TE's LFO timer.
    // Only use musical divisions when tempoSync is explicitly enabled.
    // MIDI trigger (syncType=2) can work with either Hz or musical rate —
    // it just resets the phase on note-on regardless of rateType.
    float rateType = modInfo.tempoSync ? mapSyncDivision(modInfo.syncDivision)
                                       : static_cast<float>(te::ModifierCommon::hertz);

    if (modInfo.waveform == LFOWaveform::Custom && holder) {
        // Custom waveform: update double-buffered curve data, wire callback
        holder->update(modInfo);
        const auto* activeSnapshot = holder->active.load(std::memory_order_acquire);
        DBG("[HardCorner] applyLFOProperties custom modId="
            << static_cast<int>(modInfo.id) << " name=" << modInfo.name
            << " points=" << static_cast<int>(modInfo.curvePoints.size()) << " modTypes=["
            << formatModCurveTypesForLog(modInfo) << "] snapshotTypes=["
            << (activeSnapshot ? formatSnapshotCurveTypesForLog(*activeSnapshot) : juce::String())
            << "]");
        lfo->waveParam->setParameterFromHost(
            static_cast<float>(te::LFOModifier::waveCustomCallback), juce::dontSendNotification);
        lfo->customWaveFunction.store(&CurveSnapshotHolder::evaluateCallback,
                                      std::memory_order_release);
        lfo->customWaveUserData.store(holder, std::memory_order_release);
        lfo->depthParam->setParameterFromHost(1.0f, juce::dontSendNotification);
    } else {
        if (!modInfo.curvePoints.empty()) {
            DBG("[HardCorner] applyLFOProperties not using custom curve modId="
                << static_cast<int>(modInfo.id) << " name=" << modInfo.name
                << " waveform=" << static_cast<int>(modInfo.waveform)
                << " holder=" << (holder != nullptr ? "yes" : "no") << " modTypes=["
                << formatModCurveTypesForLog(modInfo) << "]");
        }
        lfo->waveParam->setParameterFromHost(mapWaveform(modInfo.waveform),
                                             juce::dontSendNotification);
        lfo->customWaveFunction.store(nullptr, std::memory_order_release);
        lfo->depthParam->setParameterFromHost(1.0f, juce::dontSendNotification);
    }

    // In musical mode, TE uses rateParam as a multiplier on the tempo.
    // Set to 1.0 so the musical division alone controls the LFO period.
    // In Hz mode, pass the raw rate directly.
    float teRate = modInfo.tempoSync ? 1.0f : modInfo.rate;
    lfo->rateParam->setParameterFromHost(teRate, juce::dontSendNotification);
    lfo->phaseParam->setParameterFromHost(modInfo.phaseOffset, juce::dontSendNotification);
    lfo->syncTypeParam->setParameterFromHost(syncType, juce::dontSendNotification);
    lfo->rateTypeParam->setParameterFromHost(rateType, juce::dontSendNotification);

    // MIDI-triggered LFOs are gated from MAGDA's held-note model, not from
    // TE's native modifier input. That keeps top-level and rack-contained
    // LFOs consistent: note-on opens the gate, all-notes-off closes it.
    //
    // Audio-triggered LFOs are still gated by the audio sidechain path so
    // one-shots can continue through release while normal loops close on
    // the audio gate.
    lfo->setGateOnTriggerSource(modInfo.triggerMode == LFOTriggerMode::MIDI);
    if (modInfo.triggerMode == LFOTriggerMode::MIDI)
        lfo->setGated(!modInfo.running);
    else if (modInfo.triggerMode != LFOTriggerMode::Audio)
        lfo->setGated(false);
}

/**
 * @brief Map a trigger mode to TE's ADSR syncType (gate source).
 *
 * Unlike the LFO, tempo sync is a separate axis for the ADSR (it scales the
 * stage durations, not the gate), so it does not fold into syncType here.
 *   Free -> free (0): free-running, cycles A-D-R while the gate stays open.
 *   Transport -> transport (1): gate follows playback.
 *   MIDI / Audio -> note (2): gate driven by note-on/off (or the sidechain).
 */
inline float mapADSRSyncType(const ModInfo& modInfo) {
    switch (modInfo.triggerMode) {
        case LFOTriggerMode::Transport:
            return static_cast<float>(te::ModifierCommon::transport);
        case LFOTriggerMode::MIDI:
        case LFOTriggerMode::Audio:
            return static_cast<float>(te::ModifierCommon::note);
        case LFOTriggerMode::Free:
            break;
    }
    return static_cast<float>(te::ModifierCommon::free);
}

inline void applyADSRProperties(te::ADSRModifier* adsr, const ModInfo& modInfo) {
    // TE's ADSR publishes immediately in triggerNoteOn() before its timer advances.
    // An exact 0ms attack can therefore publish the pre-attack value for the first
    // audio block. Keep MAGDA's model/UI at 0, but drive TE with an inaudibly small
    // floor so "instant" attack reaches the peak on the normal timer path.
    constexpr float kInstantAttackFloorMs = 0.1f;
    adsr->attackParam->setParameterFromHost(juce::jmax(modInfo.envAttackMs, kInstantAttackFloorMs),
                                            juce::dontSendNotification);
    adsr->decayParam->setParameterFromHost(modInfo.envDecayMs, juce::dontSendNotification);
    adsr->sustainParam->setParameterFromHost(modInfo.envSustain, juce::dontSendNotification);
    adsr->releaseParam->setParameterFromHost(modInfo.envReleaseMs, juce::dontSendNotification);
    adsr->attackCurveParam->setParameterFromHost(modInfo.envAttackCurve,
                                                 juce::dontSendNotification);
    adsr->decayCurveParam->setParameterFromHost(modInfo.envDecayCurve, juce::dontSendNotification);
    adsr->releaseCurveParam->setParameterFromHost(modInfo.envReleaseCurve,
                                                  juce::dontSendNotification);
    adsr->depthParam->setParameterFromHost(1.0f, juce::dontSendNotification);

    adsr->syncTypeParam->setParameterFromHost(mapADSRSyncType(modInfo), juce::dontSendNotification);
    adsr->tempoSyncParam->setParameterFromHost(modInfo.tempoSync ? 1.0f : 0.0f,
                                               juce::dontSendNotification);

    // MAGDA carries a single musical division; apply it to every stage when
    // tempo sync is on (TE falls back to the ms params when it is off).
    const float division = mapSyncDivision(modInfo.syncDivision);
    adsr->attackSyncParam->setParameterFromHost(division, juce::dontSendNotification);
    adsr->decaySyncParam->setParameterFromHost(division, juce::dontSendNotification);
    adsr->releaseSyncParam->setParameterFromHost(division, juce::dontSendNotification);

    // Gate ownership by trigger mode:
    //   - Free: hold the gate open so the engine free-cycles A-D-R.
    //   - MIDI / Transport: gate from MAGDA's running state (TrackManager flips
    //     it on note-on/off and transport play, then re-syncs).
    //   - Audio: the audio-thread sidechain monitor owns the gate after creation
    //     (triggerNoteOn on the level rise and setGated on the drop). Property
    //     updates must not close it under an active hit.
    adsr->setSkipNativeResync(modInfo.triggerMode == LFOTriggerMode::Audio);
    adsr->setGateOnTriggerSource(modInfo.triggerMode == LFOTriggerMode::MIDI);
    if (modInfo.triggerMode == LFOTriggerMode::Free)
        adsr->setGated(false);
    else if (modInfo.triggerMode == LFOTriggerMode::MIDI ||
             modInfo.triggerMode == LFOTriggerMode::Transport)
        adsr->setGated(!modInfo.running);
}

inline void applyRandomProperties(te::RandomModifier* rnd, const ModInfo& modInfo) {
    // Timing maps exactly like the LFO path: syncType picks free/transport/note,
    // rateType picks Hz vs musical divisions, and the Hz rate doubles as a
    // tempo multiplier in synced mode (held at 1.0 so the division alone sets
    // the period).
    rnd->syncTypeParam->setParameterFromHost(mapSyncType(modInfo), juce::dontSendNotification);
    const float rateType = modInfo.tempoSync ? mapSyncDivision(modInfo.syncDivision)
                                             : static_cast<float>(te::ModifierCommon::hertz);
    rnd->rateTypeParam->setParameterFromHost(rateType, juce::dontSendNotification);
    rnd->rateParam->setParameterFromHost(modInfo.tempoSync ? 1.0f : modInfo.rate,
                                         juce::dontSendNotification);

    // Distribution shape.
    rnd->typeParam->setParameterFromHost(static_cast<float>(modInfo.randomType),
                                         juce::dontSendNotification);
    rnd->shapeParam->setParameterFromHost(modInfo.randomShape, juce::dontSendNotification);
    rnd->smoothParam->setParameterFromHost(modInfo.randomSmooth, juce::dontSendNotification);
    rnd->stepDepthParam->setParameterFromHost(modInfo.randomStepDepth, juce::dontSendNotification);

    // Output depth and bipolarity are driven per-link by ModLink.amount /
    // ModLink.bipolar (same convention as the LFO path), so keep TE's own
    // depth/bipolar at unity defaults.
    rnd->depthParam->setParameterFromHost(1.0f, juce::dontSendNotification);
}

inline void applyFollowerProperties(te::EnvelopeFollowerModifier* ef, const ModInfo& modInfo) {
    // MAGDA followers are externally fed by FollowerSourceTapPlugin. Input gain
    // must happen before source HP/LP and peak detection, so PluginManager's
    // follower source cache applies modInfo.followerGainDb. Keep TE's own gain
    // at unity to avoid a second post-detection gain stage.
    ef->gainDbParam->setParameterFromHost(0.0f, juce::dontSendNotification);
    ef->attackParam->setParameterFromHost(modInfo.followerAttackMs, juce::dontSendNotification);
    ef->holdParam->setParameterFromHost(modInfo.followerHoldMs, juce::dontSendNotification);
    ef->releaseParam->setParameterFromHost(modInfo.followerReleaseMs, juce::dontSendNotification);

    // Output depth/offset are driven per-link via ModLink.amount (same
    // convention as the other modulators), so keep TE's own depth at unity and
    // no offset. Filters stay at their disabled defaults for now.
    ef->depthParam->setParameterFromHost(1.0f, juce::dontSendNotification);
    ef->offsetParam->setParameterFromHost(0.0f, juce::dontSendNotification);
}

/**
 * @brief Set the gate on whichever gated modifier type this is (LFO or ADSR).
 *
 * Both expose the same gate API; this lets the render/sidechain paths treat
 * them uniformly without repeating the dynamic_cast ladder at every call site.
 */
inline void setModifierGated(te::Modifier* mod, bool gated) {
    if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod))
        lfo->setGated(gated);
    else if (auto* adsr = dynamic_cast<te::ADSRModifier*>(mod))
        adsr->setGated(gated);
}

/** @brief True if this gated modifier is driven externally (skips its own MIDI). */
inline bool modifierSkipsNativeResync(te::Modifier* mod) {
    if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod))
        return lfo->getSkipNativeResync();
    if (auto* adsr = dynamic_cast<te::ADSRModifier*>(mod))
        return adsr->getSkipNativeResync();
    return false;
}

/**
 * @brief Overlay a TE modifier's live output onto a ModInfo for UI animation.
 *
 * Handles both LFO (value + phase) and ADSR (value + stage). Returns true if
 * the modifier matched a known gated type.
 */
inline bool overlayModifierVisuals(ModInfo& magdaMod, te::Modifier* mod) {
    if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod)) {
        magdaMod.value = lfo->getCurrentValue();
        magdaMod.phase = lfo->getCurrentPhase();
        // For a looping custom curve the dot must follow the remapped (looped)
        // position published by the curve callback, not TE's raw 0..1 sweep.
        if (auto* holder = static_cast<CurveSnapshotHolder*>(
                lfo->customWaveUserData.load(std::memory_order_acquire))) {
            const CurveSnapshot* snap = holder->active.load(std::memory_order_acquire);
            if (snap->useLoopRegion && (snap->loopEnd - snap->loopStart) > 1.0e-4f)
                magdaMod.phase = holder->lastEffectivePhase_.load(std::memory_order_acquire);
        }
        return true;
    }
    if (auto* adsr = dynamic_cast<te::ADSRModifier*>(mod)) {
        magdaMod.value = adsr->getCurrentValue();
        magdaMod.envStage = static_cast<int>(adsr->getCurrentStage());
        return true;
    }
    if (auto* rnd = dynamic_cast<te::RandomModifier*>(mod)) {
        magdaMod.value = rnd->getCurrentValue();
        magdaMod.phase = rnd->getCurrentPhase();
        return true;
    }
    if (auto* ef = dynamic_cast<te::EnvelopeFollowerModifier*>(mod)) {
        magdaMod.value = ef->getCurrentValue();
        return true;
    }
    return false;
}

/**
 * @brief Trigger note-on on an LFO, also resetting one-shot state if applicable.
 *
 * Use this instead of calling lfo->triggerNoteOn() directly so that one-shot
 * custom waveforms restart from the beginning.
 */
inline void triggerLFONoteOnWithReset(te::LFOModifier* lfo, bool forceZeroValue = true) {
    auto* holder =
        static_cast<CurveSnapshotHolder*>(lfo->customWaveUserData.load(std::memory_order_acquire));
    if (holder)
        holder->resetOneShot();
    lfo->triggerNoteOn(forceZeroValue);
}

/**
 * @brief Clear customWaveUserData on LFO modifiers before destroying their CurveSnapshotHolders.
 *
 * Must be called before erasing/clearing curveSnapshots maps to prevent the audio thread
 * from dereferencing a dangling userData pointer in evaluateCallback.
 */
inline void clearLFOCustomWaveCallbacks(const std::vector<te::Modifier::Ptr>& modifiers) {
    for (auto& mod : modifiers) {
        if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
            lfo->customWaveFunction.store(nullptr, std::memory_order_release);
            lfo->customWaveUserData.store(nullptr, std::memory_order_release);
        }
    }
}

template <typename ModMap> inline void clearLFOCustomWaveCallbacks(const ModMap& modifierMap) {
    for (auto& [id, mod] : modifierMap) {
        if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
            lfo->customWaveFunction.store(nullptr, std::memory_order_release);
            lfo->customWaveUserData.store(nullptr, std::memory_order_release);
        }
    }
}

/**
 * @brief Move CurveSnapshotHolders to a deferred-deletion list before destroying their owner.
 *
 * After clearing LFO callback pointers (clearLFOCustomWaveCallbacks), the audio thread
 * may still be mid-call inside evaluateCallback with a pointer loaded before the null
 * store was visible. Deferring destruction ensures the holder memory stays valid until
 * the next sync cycle, by which time the audio thread has moved on.
 */
inline void deferCurveSnapshots(std::map<ModId, std::unique_ptr<CurveSnapshotHolder>>& snapshots,
                                std::vector<std::unique_ptr<CurveSnapshotHolder>>& deferred) {
    for (auto& [id, holder] : snapshots) {
        if (holder)
            deferred.push_back(std::move(holder));
    }
}

}  // namespace magda
