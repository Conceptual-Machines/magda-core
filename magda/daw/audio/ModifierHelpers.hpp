#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>

#include "../core/ModInfo.hpp"
#include "CurveSnapshot.hpp"

namespace magda {

namespace te = tracktion;

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
        {SyncDivision::Whole, RT::bar},
        {SyncDivision::Half, RT::half},
        {SyncDivision::Quarter, RT::quarter},
        {SyncDivision::Eighth, RT::eighth},
        {SyncDivision::Sixteenth, RT::sixteenth},
        {SyncDivision::ThirtySecond, RT::thirtySecond},
        {SyncDivision::DottedHalf, RT::halfD},
        {SyncDivision::DottedQuarter, RT::quarterD},
        {SyncDivision::DottedEighth, RT::eighthD},
        {SyncDivision::TripletHalf, RT::halfT},
        {SyncDivision::TripletQuarter, RT::quarterT},
        {SyncDivision::TripletEighth, RT::eighthT},
    };
    auto it = mapping.find(div);
    return static_cast<float>(it != mapping.end() ? it->second : RT::quarter);
}

/**
 * @brief Map MAGDA trigger/sync settings to TE syncType
 *
 * TE syncType: 0=free (Hz rate), 1=transport (tempo-synced), 2=note (MIDI retrigger)
 * Note mode can use either Hz rate (rateType=hertz) or musical divisions
 * (rateType=bar/quarter/etc.) depending on whether tempoSync is enabled.
 */
inline float mapSyncType(const ModInfo& modInfo) {
    // MIDI trigger → TE note mode (2): resets phase on MIDI note-on
    if (modInfo.triggerMode == LFOTriggerMode::MIDI)
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
        lfo->waveParam->setParameter(static_cast<float>(te::LFOModifier::waveCustomCallback),
                                     juce::dontSendNotification);
        lfo->customWaveFunction.store(&CurveSnapshotHolder::evaluateCallback,
                                      std::memory_order_release);
        lfo->customWaveUserData.store(holder, std::memory_order_release);
        lfo->depthParam->setParameter(1.0f, juce::dontSendNotification);
    } else {
        lfo->waveParam->setParameter(mapWaveform(modInfo.waveform), juce::dontSendNotification);
        lfo->customWaveFunction.store(nullptr, std::memory_order_release);
        lfo->depthParam->setParameter(1.0f, juce::dontSendNotification);
    }

    lfo->rateParam->setParameter(modInfo.rate, juce::dontSendNotification);
    lfo->phaseParam->setParameter(modInfo.phaseOffset, juce::dontSendNotification);
    lfo->syncTypeParam->setParameter(syncType, juce::dontSendNotification);
    lfo->rateTypeParam->setParameter(rateType, juce::dontSendNotification);
}

/**
 * @brief Trigger note-on on an LFO, also resetting one-shot state if applicable.
 *
 * Use this instead of calling lfo->triggerNoteOn() directly so that one-shot
 * custom waveforms restart from the beginning.
 */
inline void triggerLFONoteOnWithReset(te::LFOModifier* lfo) {
    auto* holder =
        static_cast<CurveSnapshotHolder*>(lfo->customWaveUserData.load(std::memory_order_acquire));
    if (holder)
        holder->resetOneShot();
    lfo->triggerNoteOn();
}

}  // namespace magda
