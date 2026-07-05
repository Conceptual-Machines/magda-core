#pragma once

#include "core/TrackInfo.hpp"

namespace magda {

/**
 * @brief Which track controls a given track exposes.
 *
 * Single source of truth for per-track-type control visibility, shared by
 * every view that renders track controls (arrange header, inspector). Views
 * decide where and how the controls are laid out; whether a control exists
 * for a track type is declared here and nowhere else.
 */
struct TrackControlsPolicy {
    // What stands in for the mute toggle.
    enum class MuteStyle {
        Standard,       // regular "M" toggle
        MasterSpeaker,  // master: speaker-icon toggle
        ChordAudition   // chord track: 3-state audition control (folds mute/solo/monitor)
    };

    bool gain = false;
    bool pan = false;
    bool mute = false;
    bool solo = false;
    bool record = false;
    bool monitor = false;
    bool automation = false;
    bool audioIn = false;
    bool midiIn = false;
    bool audioOut = false;
    bool midiOut = false;
    bool sends = false;
    bool meter = false;
    MuteStyle muteStyle = MuteStyle::Standard;

    bool anyInput() const {
        return audioIn || midiIn;
    }
    bool anyOutput() const {
        return audioOut || midiOut;
    }
    bool anyRouting() const {
        return anyInput() || anyOutput();
    }

    // No selection / controls hidden.
    static TrackControlsPolicy hidden() {
        return {};
    }

    static TrackControlsPolicy forType(TrackType type) {
        TrackControlsPolicy p;
        p.gain = true;
        p.meter = type != TrackType::Chord;  // the chord track emits no audio (yet)
        switch (type) {
            case TrackType::Audio:
                // Regular hybrid track — everything. This includes multi-out
                // parents (type Audio with children): they host the instrument,
                // so they keep record/monitor and their input routing.
                p.pan = p.mute = p.solo = p.automation = true;
                p.record = p.monitor = true;
                p.audioIn = p.midiIn = p.audioOut = p.midiOut = true;
                p.sends = true;
                break;
            case TrackType::Group:
                // Summing bus: no external input (#1689), audio output only.
                p.pan = p.mute = p.solo = p.automation = true;
                p.audioOut = p.sends = true;
                break;
            case TrackType::MultiOut:
                // Instrument-output child: mix controls plus the (locked)
                // audio-output display.
                p.pan = p.mute = p.solo = p.automation = true;
                p.audioOut = p.sends = true;
                break;
            case TrackType::Aux:
                // Return bus: mix controls only — no routing, no sends.
                p.pan = p.mute = p.solo = p.automation = true;
                break;
            case TrackType::Master:
                p.mute = true;
                p.muteStyle = MuteStyle::MasterSpeaker;
                break;
            case TrackType::Chord:
                // MIDI-only: drives instrument tracks; the audition control
                // covers mute/solo/monitor in one.
                p.mute = true;
                p.muteStyle = MuteStyle::ChordAudition;
                p.midiIn = p.midiOut = true;
                break;
        }
        return p;
    }

    static TrackControlsPolicy forTrack(const TrackInfo& track) {
        return forType(track.type);
    }

    // Multi-track selection (inspector): the shared mix controls, no routing/sends.
    static TrackControlsPolicy forMultiSelection() {
        TrackControlsPolicy p;
        p.gain = p.pan = p.mute = p.solo = p.record = p.monitor = p.automation = true;
        return p;
    }
};

}  // namespace magda
