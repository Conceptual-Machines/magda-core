#include "EngineInsertCapture.hpp"

#include <algorithm>

#include "insert/InsertCapturePlayback.hpp"
#include "insert/InsertCaptureSession.hpp"

namespace magda::daw::engine_host {

namespace {

/// Played before the window, so the graph and the outboard have settled when it opens.
constexpr double kPreRollSeconds = 1.0;

/// Past the window's end with samples still missing, the pass is not going to fill them.
constexpr double kOverrunSeconds = 2.0;

/// A transport that has not started by then is not going to.
constexpr double kStartTimeoutMs = 5000.0;

constexpr int kProgressTimerHz = 10;

/// The return port's width (PlanCompiler's InsertReturn).
constexpr int kReturnChannels = 2;

}  // namespace

/** @brief The live insert with a capture session in front of it, for the length of a pass. */
class EngineInsertCapture::Capturing final : public engine::EngineInsert {
  public:
    Capturing(EngineInsertCapture& owner, engine::DeviceKey key,
              std::unique_ptr<engine::EngineInsert> live, const engine::CaptureWindow& window,
              double sampleRate)
        : owner_(owner),
          key_(key),
          live_(std::move(live)),
          session_(*live_, window, sampleRate, kReturnChannels) {}

    ~Capturing() override {
        owner_.forget(key_, this);
    }

    void prepare(const engine::RenderContext& context) override {
        session_.prepare(context);
    }
    void reset() override {
        session_.reset();
    }
    int latencySamples() const override {
        return session_.latencySamples();
    }
    void send(const engine::BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override {
        session_.send(block, audio, midi);
    }
    void receive(const engine::BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override {
        session_.receive(block, audio, midi);
    }
    void releaseNotes(const engine::BlockInfo& block) override {
        session_.releaseNotes(block);
    }

    const engine::InsertCaptureSession& session() const {
        return session_;
    }

  private:
    EngineInsertCapture& owner_;
    engine::DeviceKey key_;
    std::unique_ptr<engine::EngineInsert> live_;
    engine::InsertCaptureSession session_;
};

EngineInsertCapture::EngineInsertCapture(InsertCaptureHost& host) : host_(host) {}

EngineInsertCapture::~EngineInsertCapture() {
    stopTimer();
    sessions_.clear();
}

bool EngineInsertCapture::exportNeedsCapturePass() const {
    return !host_.capturableInserts().empty();
}

bool EngineInsertCapture::startCapturePass(double startSec, double endSec, double renderSampleRate,
                                           std::function<void(bool)> onFinished) {
    juce::ignoreUnused(renderSampleRate);

    if (pass_.has_value() || !(endSec > startSec)) {
        lastError_ = PassError::SetupFailed;
        return false;
    }
    lastError_ = PassError::None;
    captures_.clear();

    auto keys = host_.capturableInserts();
    if (keys.empty())
        return false;

    const auto rate = host_.liveSampleRate();
    if (!(rate > 0.0)) {
        lastError_ = PassError::SetupFailed;
        return false;
    }

    // Past the range by the plan's compensation, which a render reads the return over.
    const engine::CaptureWindow window{.startSeconds = startSec,
                                       .endSeconds = endSec + host_.livePlanLatencySeconds()};

    pass_ = Pass{.window = window,
                 .keys = std::move(keys),
                 .savedSeconds = host_.transportSeconds(),
                 .savedLooping = host_.transportLooping(),
                 .startedMs = juce::Time::getMillisecondCounterHiRes(),
                 .onFinished = std::move(onFinished)};

    const auto armed = host_.rewrapInserts(
        [this, window, rate](engine::DeviceKey key, std::unique_ptr<engine::EngineInsert> live) {
            auto capturing = std::make_unique<Capturing>(*this, key, std::move(live), window, rate);
            sessions_[key] = capturing.get();
            return capturing;
        });

    // All or nothing: an insert without a session would render its return as silence.
    const auto everyInsert = std::ranges::all_of(
        pass_->keys, [this](engine::DeviceKey key) { return sessions_.contains(key); });
    if (!armed || !everyInsert) {
        pass_.reset();
        sessions_.clear();
        host_.rewrapInserts(nullptr);
        lastError_ = PassError::SetupFailed;
        return false;
    }

    host_.playFrom(std::max(0.0, startSec - kPreRollSeconds));
    startTimerHz(kProgressTimerHz);
    return true;
}

void EngineInsertCapture::cancelCapturePass() {
    if (pass_.has_value())
        finish(false);
}

double EngineInsertCapture::getProgress() const {
    if (!pass_.has_value() || pass_->keys.empty())
        return 0.0;

    const auto total = static_cast<double>(pass_->window.samplesAt(host_.liveSampleRate()));
    if (!(total > 0.0))
        return 0.0;

    auto slowest = 1.0;
    for (const auto key : pass_->keys) {
        const auto found = sessions_.find(key);
        const auto missing = found != sessions_.end()
                                 ? static_cast<double>(found->second->session().missingSamples())
                                 : total;
        slowest = std::min(slowest, 1.0 - missing / total);
    }
    return std::clamp(slowest, 0.0, 1.0);
}

std::unique_ptr<engine::EngineInsert> EngineInsertCapture::playbackFor(
    engine::DeviceKey key, const engine::CaptureWindow& window,
    const engine::RenderContext& context) const {
    const auto found = captures_.find(key);
    if (found == captures_.end())
        return nullptr;

    return engine::InsertCapturePlayback::create(found->second, window, context);
}

void EngineInsertCapture::forget(engine::DeviceKey key, const void* session) {
    if (const auto found = sessions_.find(key);
        found != sessions_.end() && found->second == session)
        sessions_.erase(found);
}

void EngineInsertCapture::timerCallback() {
    if (!pass_.has_value()) {
        stopTimer();
        return;
    }

    const auto playing = host_.transportPlaying();
    pass_->sawPlaying = pass_->sawPlaying || playing;

    const auto complete = std::ranges::all_of(pass_->keys, [this](engine::DeviceKey key) {
        const auto found = sessions_.find(key);
        return found != sessions_.end() && found->second->session().missingSamples() == 0;
    });
    if (complete) {
        finish(true);
        return;
    }

    // Stopping the transport mid-pass is a cancel.
    if (pass_->sawPlaying && !playing) {
        finish(false);
        return;
    }

    const auto overran = host_.transportSeconds() > pass_->window.endSeconds + kOverrunSeconds;
    const auto neverStarted =
        !pass_->sawPlaying &&
        juce::Time::getMillisecondCounterHiRes() - pass_->startedMs > kStartTimeoutMs;
    if (overran || neverStarted) {
        lastError_ = PassError::CaptureFailed;
        finish(false);
    }
}

void EngineInsertCapture::finish(bool success) {
    stopTimer();
    if (!pass_.has_value())
        return;

    auto pass = std::move(*pass_);
    pass_.reset();

    host_.stopAt(pass.savedSeconds, pass.savedLooping);

    if (success) {
        for (const auto key : pass.keys) {
            const auto found = sessions_.find(key);
            auto capture = found != sessions_.end() ? found->second->session().take()
                                                    : engine::InsertCapture{};
            if (!capture.complete()) {
                success = false;
                lastError_ = PassError::CaptureFailed;
                break;
            }
            captures_.insert_or_assign(key, std::move(capture));
        }
    }

    sessions_.clear();
    host_.rewrapInserts(nullptr);

    if (!success)
        captures_.clear();

    if (pass.onFinished)
        pass.onFinished(success);
}

}  // namespace magda::daw::engine_host
