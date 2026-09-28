#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

#include "insert_capture/InsertRenderCapture.hpp"

namespace tracktion::inline engine {
class Edit;
}  // namespace tracktion::inline engine

namespace magda {

/**
 * @brief Under-the-hood capture pass that lets offline export bounce External
 *        FX / External Instrument inserts (#1623).
 *
 * The offline renderer cannot drive hardware, so a routed insert renders
 * silent. When export detects one, it calls startCapturePass() first: the
 * export range plays once through the live engine while a hidden
 * InsertCapturePlugin after each routed insert records its audio return to a
 * temp file. On completion the taps stay in the graph, switched to playback
 * mode — during the offline render they substitute the captured returns at
 * the exact chain position (PDC-aligned by construction). cleanupAfterRender()
 * removes the taps and temp files.
 *
 * Nothing user-visible changes: no clips, no bypassing, no persisted state.
 * One pass at a time; all methods are message-thread only.
 */
class InsertRenderCaptureService final : public InsertRenderCapture, private juce::Timer {
  public:
    explicit InsertRenderCaptureService(tracktion::engine::Edit& edit);
    ~InsertRenderCaptureService() override;

    /** True when the edit has an enabled external insert with both a send and
        a return configured -- i.e. export needs the capture pass. */
    bool exportNeedsCapturePass() const override;

    PassError getLastPassError() const override {
        return lastError_;
    }

    /** Start the real-time pass over [startSec, endSec). renderSampleRate is
        the upcoming offline render's rate: captures are recorded at the live
        device rate and resampled to it on completion (the playback taps read
        the file 1:1). onFinished(success) fires on the message thread; on
        success the taps are already switched to playback mode for the offline
        render. Returns false when a pass is already running, no insert
        qualifies, or arming failed (see getLastPassError()). */
    bool startCapturePass(double startSec, double endSec, double renderSampleRate,
                          std::function<void(bool)> onFinished) override;

    /** Abort the running pass; taps and partial files are removed, then
        onFinished(false) fires. */
    void cancelCapturePass() override;

    bool isCapturing() const override {
        return pass_ != nullptr;
    }

    /** Fraction 0..1 of the capture window written so far (UI polls). */
    double getProgress() const override;

    /** Remove the playback taps and temp files after the offline render (also
        safe to call when nothing is armed). */
    void cleanupAfterRender() override;

  private:
    // Taps + their temp files, alive from pass start until cleanupAfterRender.
    struct Taps;

    void timerCallback() override;
    void finishPass(bool success);
    void removeTaps();

    tracktion::engine::Edit& edit_;
    std::unique_ptr<Taps> taps_;

    struct ActivePass {
        double windowStartSec = 0.0;
        double windowEndSec = 0.0;
        double renderSampleRate = 0.0;
        double savedPositionSec = 0.0;
        bool savedLooping = false;
        std::function<void(bool)> onFinished;
    };
    std::unique_ptr<ActivePass> pass_;
    PassError lastError_ = PassError::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertRenderCaptureService)
};

}  // namespace magda
