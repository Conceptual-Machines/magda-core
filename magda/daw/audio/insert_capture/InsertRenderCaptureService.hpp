#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace tracktion {
inline namespace engine {
class Edit;
}  // namespace engine
}  // namespace tracktion

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
class InsertRenderCaptureService : private juce::Timer {
  public:
    explicit InsertRenderCaptureService(tracktion::engine::Edit& edit);
    ~InsertRenderCaptureService() override;

    /** True when the edit has an enabled external insert with both a send and
        a return configured — i.e. export needs the capture pass. */
    bool exportNeedsCapturePass() const;

    /** Start the real-time pass over [startSec, endSec). onFinished(success)
        fires on the message thread; on success the taps are already switched
        to playback mode for the offline render. Returns false when a pass is
        already running or no insert qualifies. */
    bool startCapturePass(double startSec, double endSec, std::function<void(bool)> onFinished);

    /** Abort the running pass; taps and partial files are removed, then
        onFinished(false) fires. */
    void cancelCapturePass();

    bool isCapturing() const {
        return pass_ != nullptr;
    }

    /** Fraction 0..1 of the capture window written so far (UI polls). */
    double getProgress() const;

    /** Remove the playback taps and temp files after the offline render (also
        safe to call when nothing is armed). */
    void cleanupAfterRender();

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
        double savedPositionSec = 0.0;
        bool savedLooping = false;
        std::function<void(bool)> onFinished;
    };
    std::unique_ptr<ActivePass> pass_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertRenderCaptureService)
};

}  // namespace magda
