#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <set>
#include <vector>

#include "../audio/analysis/MaskingDetector.hpp"
#include "../audio/analysis/TrackMeasurer.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Owns the per-track measurement layer's enablement and polling (#1388).
 *
 * The DSP lives in TrackMeasurer; the always-on tap is TrackMeasurementPlugin;
 * the tap lifecycle (insert/remove post-fader) lives in PluginManager. This
 * manager sits on top and decides WHEN measurement runs, polls the taps, and
 * serves snapshots to consumers (the Levels meter and the mixing agent).
 *
 * Enablement contract (see the dormant-until-consumed / user-disable design):
 *   - A global "Mix Analysis" kill switch. Off => every tap is removed and the
 *     poll timer stops; nothing runs and nothing is inserted.
 *   - Per-track enable, requested by consumers. A track only measures while it
 *     is enabled AND the global switch is on. The desired per-track set is
 *     remembered across global toggles so re-enabling restores it.
 *   - Taps are inserted lazily on first enable, not on every track up front.
 *
 * Message-thread only. Reaches the tap plugins through the audio bridge.
 */
class TrackMeasurementListener {
  public:
    virtual ~TrackMeasurementListener() = default;
    /// Fired after each poll with the set of tracks that have fresh snapshots.
    virtual void trackMeasurementsUpdated() {}
};

class TrackMeasurementManager : private juce::Timer {
  public:
    static TrackMeasurementManager& getInstance();

    TrackMeasurementManager(const TrackMeasurementManager&) = delete;
    TrackMeasurementManager& operator=(const TrackMeasurementManager&) = delete;

    // --- Global kill switch ("Mix Analysis") --------------------------------
    void setGlobalEnabled(bool shouldEnable);
    bool isGlobalEnabled() const {
        return globalEnabled_;
    }

    // --- Per-track enablement (requested by consumers) ----------------------
    void setTrackEnabled(TrackId trackId, bool shouldEnable);
    bool isTrackEnabled(TrackId trackId) const {
        return enabledTracks_.count(trackId) > 0;
    }

    // --- Snapshots ----------------------------------------------------------
    /// Latest measurement for a track. `valid == false` if none yet / not enabled.
    daw::audio::TrackMeasurementSnapshot getSnapshot(TrackId trackId) const;
    /// All currently-held snapshots (enabled tracks with data), for the agent.
    std::vector<std::pair<TrackId, daw::audio::TrackMeasurementSnapshot>> getAllSnapshots() const;

    // --- Masking analysis (#1390) -------------------------------------------
    /// Arm/disarm masking band capture on the enabled taps. Heavier than the
    /// scalar metering, so it is off unless a masking pass wants it.
    void setMaskingAnalysisEnabled(bool shouldEnable);
    bool isMaskingAnalysisEnabled() const {
        return maskingEnabled_;
    }
    /// Gather the enabled tracks' current band spectra and detect inter-track
    /// masking. Returns {} until capture has been armed and audio has flowed.
    std::vector<daw::audio::MaskingFinding> getMaskingFindings(
        const daw::audio::MaskingOptions& opts = {}) const;
    /// Copy the latest numSamples of a track's captured mono signal (the masking
    /// spectrum ring) into dest, for a full-resolution overlay FFT (#1400). The
    /// overlay runs the same pipeline as the device's own trace on these samples.
    /// Returns the ring's running sample count (0 if no live tap / no audio yet).
    size_t readTrackSpectrumSamples(TrackId trackId, float* dest, int numSamples,
                                    double& sampleRateOut) const;

    void addListener(TrackMeasurementListener* l) {
        listeners_.add(l);
    }
    void removeListener(TrackMeasurementListener* l) {
        listeners_.remove(l);
    }

    /// Poll cadence. Momentary loudness + peaks update fast; this is plenty.
    static constexpr int kPollIntervalMs = 33;

  private:
    TrackMeasurementManager() = default;

    void timerCallback() override;

    // Reconcile a single track's tap with (global && desired) state.
    void applyTrack(TrackId trackId);
    // Start/stop the poll timer based on whether anything is active.
    void updateTimer();

    bool globalEnabled_ = false;
    bool maskingEnabled_ = false;      // masking band capture armed
    std::set<TrackId> enabledTracks_;  // desired per-track state
    std::map<TrackId, daw::audio::TrackMeasurementSnapshot> latest_;
    juce::ListenerList<TrackMeasurementListener> listeners_;
};

}  // namespace magda
