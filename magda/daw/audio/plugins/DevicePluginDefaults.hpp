#pragma once

#include "core/TypeIds.hpp"

namespace magda::daw::audio {

/**
 * @brief What a freshly created device starts from, when its own state says
 *        nothing (#2663).
 *
 * The user's last-used settings rather than the class's: adding an analyser
 * gives you the trace you were last looking at. Supplied by the host, through
 * DeviceServices for a device created in a session and through
 * DevicePluginCreationContext for one created detached.
 */
struct DevicePluginDefaults {
    struct Oscilloscope {
        float timebaseMs = 10.0f;
        /// Index into the analyzer palette.
        int traceColour = 0;
    } oscilloscope;

    struct Spectrum {
        int fftOrder = 11;
        float slopeDbPerOct = 4.5f;
        float smoothing = 0.5f;
        int traceColour = 0;
    } spectrum;

    struct MidiReceive {
        TrackId sourceTrackId = INVALID_TRACK_ID;
        bool replaceExistingMidi = false;
    } midiReceive;
};

}  // namespace magda::daw::audio
