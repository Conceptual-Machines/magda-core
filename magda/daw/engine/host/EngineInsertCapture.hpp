#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "../../audio/insert_capture/InsertRenderCapture.hpp"
#include "EngineRuntimeFactory.hpp"
#include "insert/InsertCapture.hpp"

/**
 * @file EngineInsertCapture.hpp
 * @brief The live pass that records hardware inserts for a render, on magda::engine (#2279).
 *
 * The range plays once through the live session with an InsertCaptureSession in
 * front of each insert. The captures are kept until the render that follows asks
 * for an InsertCapturePlayback in each insert's place, and dropped after it.
 */

namespace magda::daw::engine_host {

/** @brief What the pass needs of the host whose inserts it records. Message thread. */
class InsertCaptureHost {
  public:
    virtual ~InsertCaptureHost() = default;

    /// The hardware inserts the live plan runs with an audio return.
    virtual std::vector<engine::DeviceKey> capturableInserts() const = 0;

    /// Put @p wrapper in front of every live insert, or take it away with null, and
    /// republish. False when the publish did not happen.
    virtual bool rewrapInserts(EngineRuntimeFactory::InsertWrapper wrapper) = 0;

    /// The running device's rate, or zero with none running.
    virtual double liveSampleRate() const = 0;

    /// How far past its range a render reads the return: the live plan's compensation.
    virtual double livePlanLatencySeconds() const = 0;

    virtual bool transportPlaying() const = 0;
    virtual double transportSeconds() const = 0;
    virtual bool transportLooping() const = 0;

    /// Play from @p seconds with the loop off.
    virtual void playFrom(double seconds) = 0;

    /// Stop and stand at @p seconds with the loop as @p looping.
    virtual void stopAt(double seconds, bool looping) = 0;
};

class EngineInsertCapture final : public InsertRenderCapture, private juce::Timer {
  public:
    explicit EngineInsertCapture(InsertCaptureHost& host);
    ~EngineInsertCapture() override;

    bool exportNeedsCapturePass() const override;

    PassError getLastPassError() const override {
        return lastError_;
    }

    bool startCapturePass(double startSec, double endSec, double renderSampleRate,
                          std::function<void(bool)> onFinished) override;
    void cancelCapturePass() override;

    bool isCapturing() const override {
        return pass_.has_value();
    }

    double getProgress() const override;

    void cleanupAfterRender() override {
        captures_.clear();
    }

    /**
     * @brief What plays @p key's insert in a render over @p window, or null.
     *
     * Null with no capture of it, or one that InsertCapturePlayback refuses: incomplete,
     * short of @p window, or narrower than @p context.
     */
    std::unique_ptr<engine::EngineInsert> playbackFor(engine::DeviceKey key,
                                                      const engine::CaptureWindow& window,
                                                      const engine::RenderContext& context) const;

    /// A capturing insert the store is destroying, so the pass stops reading it.
    void forget(engine::DeviceKey key, const void* session);

  private:
    class Capturing;

    struct Pass {
        engine::CaptureWindow window;
        std::vector<engine::DeviceKey> keys;
        double savedSeconds = 0.0;
        bool savedLooping = false;
        bool sawPlaying = false;
        double startedMs = 0.0;
        std::function<void(bool)> onFinished;
    };

    void timerCallback() override;
    void finish(bool success);

    InsertCaptureHost& host_;
    std::optional<Pass> pass_;
    PassError lastError_ = PassError::None;

    /// Owned by the live store; forgotten as it destroys them.
    std::map<engine::DeviceKey, Capturing*> sessions_;

    std::map<engine::DeviceKey, engine::InsertCapture> captures_;
};

}  // namespace magda::daw::engine_host
