#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <utility>
#include <vector>

#include "ChainNodePath.hpp"
#include "TypeIds.hpp"

namespace magda {

// ============================================================================
// GAIN STAGING (algorithmic phase)
// ============================================================================
//
// Mirrors the LinkModeManager pattern: a singleton that holds a mode + per-
// device state and notifies listeners, so device headers can reflect staging
// the same way they reflect link mode.
//
// ## Flow
//
//   1. User starts a pass on a track  -> Collecting
//   2. User plays the desired section; a timer samples each device's peak
//      meter and keeps a running max ("peak hold") per device.
//   3. User stops the pass            -> back to Idle
//      The engine walks the chain, and for each device that saw signal it
//      moves the device's OUTPUT VOLUME so its captured peak lands at the
//      target headroom. All moves are applied as ONE undo step, and the moved
//      faders keep a persistent mark (see appliedDeltas_).
//
// This phase is deliberately order-blind: each device is targeted on its own.
// The musical, order-aware decisions (drive a saturator, respect a compressor
// threshold, account for a limiter at the end) are the job of the later AI
// phase, which reuses the same capture + apply machinery.
//
// ============================================================================

/** Lowest level we report; treated as "silence / no signal seen". */
inline constexpr float kGainStageSilenceDb = -100.0f;

/** Default headroom target for a staged device's output PEAK. Note this is a
 *  peak meter, so the classic -18 dBFS (an RMS/alignment figure) reads far too
 *  quiet here; -12 dBFS peak leaves healthy headroom without collapsing level. */
inline constexpr float kGainStageDefaultTargetDb = -12.0f;

/** Output-volume clamp, matching the device gain control range. */
inline constexpr float kGainStageMinGainDb = -60.0f;
inline constexpr float kGainStageMaxGainDb = 12.0f;

/** Overall mode of a staging pass. */
enum class GainStagingMode {
    Idle,        // nothing happening
    Collecting,  // capturing peak holds while the transport plays
};

/** Per-device state shown on the device header. */
enum class DeviceGainStageState {
    Idle,        // not in this pass, or saw no signal
    Collecting,  // capturing this device's peak
    Adjusted,    // output volume was moved at stop
    Clipped,     // captured peak reached/exceeded 0 dBFS during the pass
};

/** Per-device staging data the header reads to draw its badge. */
struct DeviceGainStageInfo {
    DeviceGainStageState state = DeviceGainStageState::Idle;
    float capturedPeakDb = kGainStageSilenceDb;  // max peak seen during collection
    float appliedDeltaDb = 0.0f;                 // gain change applied at stop
    bool clipped = false;                        // captured peak >= 0 dBFS
};

/** Listener interface, mirroring LinkModeManagerListener. */
class GainStagingListener {
  public:
    virtual ~GainStagingListener() = default;

    /** Mode changed (Idle/Collecting/Staged). trackId is the pass's track. */
    virtual void gainStagingModeChanged([[maybe_unused]] GainStagingMode mode,
                                        [[maybe_unused]] TrackId trackId) {}

    /** A single device's staging state changed. Identified by full path: under
     *  section-local DeviceIds a bare id is ambiguous (an FX device and a
     *  post-FX device on one track routinely share an id). */
    virtual void deviceGainStageChanged([[maybe_unused]] const ChainNodePath& devicePath,
                                        [[maybe_unused]] const DeviceGainStageInfo& info) {}
};

/**
 * @brief Coordinates a gain-staging pass over a track's signal chain.
 *
 * Message-thread only. Reads device peak meters through the audio bridge and
 * writes device output volumes through TrackManager.
 */
class GainStagingManager : private juce::Timer {
  public:
    static GainStagingManager& getInstance();

    GainStagingManager(const GainStagingManager&) = delete;
    GainStagingManager& operator=(const GainStagingManager&) = delete;

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    GainStagingMode getMode() const {
        return mode_;
    }

    bool isCollecting() const {
        return mode_ == GainStagingMode::Collecting;
    }

    TrackId getActiveTrack() const {
        return activeTrackId_;
    }

    float getTargetDb() const {
        return targetDb_;
    }

    void setTargetDb(float db);

    // Whether the next pass should defer to the AI agent. Stored here so the
    // dialog can set it before startCollection; the algorithmic path runs until
    // the agent (phase 2) is wired in.
    bool getUseAi() const {
        return useAi_;
    }

    void setUseAi(bool useAi) {
        useAi_ = useAi;
    }

    // ------------------------------------------------------------------
    // Pass control
    // ------------------------------------------------------------------

    /** Begin a staging pass on the given track's chain (-> Collecting). */
    void startCollection(TrackId trackId);

    /** Stop the pass, compute + apply the gain moves (-> Staged). */
    void stopCollection();

    /** Clear staging state and badges; applied gains are kept (-> Idle). */
    void reset();

    /**
     * Header-button convenience for the non-idle case:
     *   Collecting -> stopCollection()
     * (The Idle case opens the pre-pass dialog from the UI, not here.)
     */
    void toggle(TrackId trackId);

    // ------------------------------------------------------------------
    // AI pass (phase 2)
    // ------------------------------------------------------------------
    // stopCollection() applies the flat-target cascade. For an AI pass the UI
    // instead calls finishCaptureForAi() to freeze + read the capture, runs the
    // agent off the message thread, then applyAiMoves() with the decisions. On
    // failure or cancel the UI calls reset() to drop the frozen capture.

    /** One parameter setting, sent so the agent can reason about thresholds,
     *  drive, ceilings, etc. (populated for MAGDA/internal devices only). */
    struct ParamSnapshot {
        juce::String name;
        float value = 0.0f;  // real units (dB, Hz, %, ...)
        juce::String unit;
    };

    /** One device's captured state, handed to the AI agent. */
    struct DeviceSnapshot {
        DeviceId deviceId = INVALID_DEVICE_ID;
        ChainNodePath path;  // unique device identity (deviceId alone is ambiguous)
        juce::String name;
        juce::String pluginId;
        bool isInstrument = false;
        float capturedPeakDb = kGainStageSilenceDb;
        float currentGainDb = 0.0f;
        float suggestedGainDb = 0.0f;       // flat-target trim (the algorithmic answer)
        std::vector<ParamSnapshot> params;  // current settings, MAGDA devices only
    };

    /** Stop the timer and return the captured per-device snapshot WITHOUT
     *  applying anything. The capture is retained for a following applyAiMoves()
     *  or reset(). Mode returns to Idle. */
    std::vector<DeviceSnapshot> finishCaptureForAi();

    /** Apply externally-decided output gains (the AI result) as one undoable
     *  command, marking the moved devices, then clear the frozen capture.
     *  Keyed by path so post-FX and FX devices that share an id stay distinct. */
    void applyAiMoves(const std::vector<std::pair<ChainNodePath, float>>& deviceNewGainDb);

    /** Per-device staging info for the *current* pass, or nullptr. Transient:
     *  cleared by reset(). */
    const DeviceGainStageInfo* getDeviceInfo(const ChainNodePath& devicePath) const;

    // ------------------------------------------------------------------
    // Persistent applied marks
    // ------------------------------------------------------------------
    // Independent of the transient pass state: a record of the gain move
    // currently applied to each device, so the UI can mark which devices the
    // staging touched even after the pass banner is cleared. Cleared when the
    // user manually changes that device's gain, when a new pass restages the
    // device, or on undo.

    /** Applied gain delta (dB) for a device, or nullptr if none is recorded. */
    const float* getAppliedDelta(const ChainNodePath& devicePath) const;

    /** Record an applied move (called by the staging command on do/redo). */
    void markApplied(const ChainNodePath& devicePath, float deltaDb);

    /** Drop a device's applied mark (manual gain edit, restage, or undo). */
    void clearApplied(const ChainNodePath& devicePath);

    // ------------------------------------------------------------------
    // Listeners
    // ------------------------------------------------------------------

    void addListener(GainStagingListener* listener);
    void removeListener(GainStagingListener* listener);

  private:
    GainStagingManager() = default;
    ~GainStagingManager() override;

    void timerCallback() override;

    struct StagedDevice {
        DeviceId deviceId = INVALID_DEVICE_ID;
        ChainNodePath path;
    };

    // Collects every device in the active track's chain (fx tree + post-fx),
    // in signal order, into staged_ and seeds info_ with Collecting state.
    void buildStagedDeviceList(TrackId trackId);

    // Reads max(peakL, peakR) for a device from live metering. Returns false
    // if metering is unavailable or the device has no meter entry.
    bool readDevicePeakLinear(const ChainNodePath& devicePath, float& peakLinearOut) const;

    void notifyMode();
    void notifyDevice(const ChainNodePath& devicePath);

    GainStagingMode mode_ = GainStagingMode::Idle;
    TrackId activeTrackId_ = INVALID_TRACK_ID;
    float targetDb_ = kGainStageDefaultTargetDb;
    bool useAi_ = false;

    std::vector<StagedDevice> staged_;
    std::map<ChainNodePath, DeviceGainStageInfo> info_;  // transient: current pass
    std::map<ChainNodePath, float> appliedDeltas_;       // persistent: applied marks
    std::vector<GainStagingListener*> listeners_;
};

}  // namespace magda
