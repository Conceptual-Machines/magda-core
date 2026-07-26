// Stem separation orchestration (issue #1288).
//
// The entry point the UI calls. Owns a single-worker thread pool, so splits
// queue up and run serially (a full-quality separation saturates the CPU;
// running two at once would starve both). Given an audio clip, it decodes the
// clip's WHOLE source file, runs the chosen separation engine off the message
// thread, writes one WAV per stem into the project's stems media folder, then
// hops back to the message thread and builds the result as one undo step:
// one audio track per stem, grouped beneath the source track, each carrying a
// duplicate of the source clip pointing at its stem file. Duplicating the
// source clip (rather than creating a fresh one) keeps offset, stretch, warp
// markers and fades aligned by construction, and separating the whole file
// (rather than the clip's audible range) keeps the stem clips freely
// resizable afterwards. The source clip is never touched.

#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

#include "ClipTypes.hpp"
#include "TypeIds.hpp"

namespace magda::stems {

class StemSeparationService {
  public:
    // Separation engines the UI can offer. Availability differs: HPSS is pure
    // DSP and always present; the ONNX engines need their model downloaded.
    enum class Engine {
        Hpss,
        Demucs,
        Spleeter,
    };

    static StemSeparationService& getInstance();

    // True when the engine can run right now (backend compiled in and, for
    // model-based engines, weights installed). UI uses this to enable items
    // or deep-link to the model download instead.
    [[nodiscard]] bool isEngineAvailable(Engine engine);

    // True while a split is running or queued; the UI shows progress for it.
    [[nodiscard]] bool isBusy() const;

    // Result: groupTrackId is the created stem group (INVALID_TRACK_ID on
    // failure; error then holds a user-facing message). Fires on the message
    // thread.
    using Completion = std::function<void(TrackId groupTrackId, juce::String error)>;

    // Global activity feed for the main window's loading banner: fires on
    // the message thread when a split starts (running, progress 0), on each
    // whole-percent progress tick, and once when it ends (running false).
    // Set once at startup by the main window; cleared on its destruction.
    using ActivityCallback = std::function<void(bool running, float progress)>;
    void setActivityCallback(ActivityCallback callback);

    // Split the source file of an existing audio clip into stems. No-op
    // (error completion) if the clip isn't audio or the engine is unavailable.
    void splitClipIntoStems(ClipId sourceClipId, Engine engine, Completion onComplete);

    // Request cancellation of the running split and any queued splits.
    // Completion callbacks still fire after their pre-created tracks are
    // cleaned up.
    void cancelAll();

    StemSeparationService(const StemSeparationService&) = delete;
    StemSeparationService& operator=(const StemSeparationService&) = delete;

  private:
    StemSeparationService();
    ~StemSeparationService();

    std::unique_ptr<juce::ThreadPool> pool_;
    std::atomic<int> pendingJobs_{0};
    std::atomic<bool> cancelRequested_{false};
    ActivityCallback activityCallback_;  // message thread only
};

}  // namespace magda::stems
