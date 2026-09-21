#pragma once

#include <functional>

namespace magda {

/**
 * @brief The real-time pass that lets a render include hardware inserts (#1623, #2279).
 *
 * A render runs faster than the outside world, so export plays the range once live
 * while each routed insert's return is recorded, and the render plays the recordings
 * back where the inserts stand. Nothing user-visible changes. One pass at a time;
 * message thread only. Each engine answers with its own.
 */
class InsertRenderCapture {
  public:
    virtual ~InsertRenderCapture() = default;

    /** True when the project has a hardware insert with a send and a return that plays. */
    virtual bool exportNeedsCapturePass() const = 0;

    /** Why the last pass could not deliver captures. */
    enum class PassError {
        None,           // succeeded, was cancelled, or no insert qualifies
        SetupFailed,    // a qualifying insert could not be armed, or a pass was running
        CaptureFailed,  // a recording failed or could not be prepared for the render
    };

    virtual PassError getLastPassError() const = 0;

    /** Start the pass over [startSec, endSec) for a render at renderSampleRate.
        onFinished(success) fires on the message thread; on success the render that
        follows plays the recordings. False when a pass is already running, no insert
        qualifies, or arming failed (see getLastPassError()). */
    virtual bool startCapturePass(double startSec, double endSec, double renderSampleRate,
                                  std::function<void(bool)> onFinished) = 0;

    /** Abort the running pass; onFinished(false) fires. */
    virtual void cancelCapturePass() = 0;

    virtual bool isCapturing() const = 0;

    /** Fraction 0..1 of the window written so far. */
    virtual double getProgress() const = 0;

    /** Drop the recordings once the render is done; safe when nothing ran. */
    virtual void cleanupAfterRender() = 0;
};

}  // namespace magda
