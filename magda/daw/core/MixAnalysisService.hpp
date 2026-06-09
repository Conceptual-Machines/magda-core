#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <optional>
#include <set>

#include "../../agents/mixing_agent.hpp"  // MixAnalysisAgent::Input (measured data)
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Owns mix-analysis *measurement* gathering, decoupled from the agent.
 *
 * Splits the old console flow in two: this service produces the measured
 * MixAnalysisAgent::Input (per-track levels + masking + tonal/timeline) and
 * caches it; the LLM step stays in the console and is opt-in. Both the mixer
 * toggle-rail Analyze button (which shows a modal of the measured findings) and
 * the console (which attaches the latest measurement as agent context) read this
 * one source.
 *
 * Which channels are analysed is driven by the mixer's selection (empty = the
 * whole mix); the only mode axis is how they're measured:
 *   - Live:    instant, no render. Arms the TrackMeasurementManager taps; the
 *              user plays the mix; stopLiveCapture() gathers snapshots + masking.
 *   - Offline: render the selected stems + master and measure them
 *              (OfflineMixAnalysis Deep over the selection).
 *
 * Message-thread only. No LLM call here.
 */
class MixAnalysisService {
  public:
    enum class Mode { Live, Offline };

    class Listener {
      public:
        virtual ~Listener() = default;
        /// Fired on any state change: run started / ready / failed / capturing.
        virtual void mixAnalysisChanged() {}
    };

    static MixAnalysisService& getInstance();

    MixAnalysisService(const MixAnalysisService&) = delete;
    MixAnalysisService& operator=(const MixAnalysisService&) = delete;

    // --- Offline (render the selected stems + master) -----------------------
    /// Render + measure off the message thread, no agent. Caches + broadcasts on
    /// completion. No-op if a run/capture is already in flight.
    void runOffline();

    // --- Live capture (instant, no render) ----------------------------------
    void startLiveCapture();  // arm the taps; the user then plays the mix
    void stopLiveCapture();   // gather snapshots + masking into a cached Input
    bool isCapturing() const {
        return capturing_;
    }

    /// Abandon an in-flight offline run / live capture and reset. The render
    /// can't be interrupted, so a late result is dropped by the run-id guard.
    void cancel();

    /// Human-readable scope from the current mixer selection: "the full mix"
    /// (nothing or the master selected) or "N selected channels". For menus.
    juce::String scopeDescription() const;
    /// Human-readable time range an offline run will cover: "loop region" when the
    /// transport is looping (only that part is rendered), else "whole song".
    juce::String rangeDescription() const;

    bool isBusy() const {
        return busy_;
    }
    Mode busyMode() const {
        return busyMode_;
    }
    juce::String progressText() const {
        return progressText_;
    }
    juce::String lastError() const {
        return lastError_;
    }

    /// The cached measured data for a mode (nullopt if never run / cleared).
    std::optional<MixAnalysisAgent::Input> cached(Mode mode) const;
    /// The most recently produced measurement of any mode.
    std::optional<MixAnalysisAgent::Input> latest() const;

    void addListener(Listener* l) {
        listeners_.add(l);
    }
    void removeListener(Listener* l) {
        listeners_.remove(l);
    }

  private:
    MixAnalysisService() = default;

    void store(Mode mode, MixAnalysisAgent::Input input);
    void setBusy(bool busy, Mode mode);
    void restoreCaptureState();  // undo what startLiveCapture armed
    MixAnalysisAgent::Input buildLiveInput() const;

    bool busy_ = false;
    Mode busyMode_ = Mode::Offline;
    bool capturing_ = false;
    int runId_ = 0;  // offline cancel guard (drops a stale run's late result)
    // Cancel handle for the in-flight offline render; set true to actually abort
    // it (so it stops rendering, not just hides). std::shared_ptr<std::atomic<bool>>
    // is OfflineMixAnalysis::CancelToken (kept as the raw type to avoid the include).
    std::shared_ptr<std::atomic<bool>> offlineCancel_;
    juce::String progressText_;
    juce::String lastError_;

    // Live-capture restore bookkeeping (mirrors the console's): only undo the
    // tap enablement we added, so another consumer (the Levels meter) is left
    // alone.
    std::set<TrackId> captureAddedTracks_;
    bool captureAddedGlobal_ = false;
    bool captureAddedMasking_ = false;

    std::optional<MixAnalysisAgent::Input> cacheLive_, cacheOffline_;
    Mode latestMode_ = Mode::Offline;
    bool haveLatest_ = false;

    juce::ListenerList<Listener> listeners_;
};

}  // namespace magda
