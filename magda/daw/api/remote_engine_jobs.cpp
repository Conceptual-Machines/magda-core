#include "remote_engine_jobs.hpp"

#include <mutex>
#include <thread>

namespace magda::remote {
namespace {

class RenderWork final : public std::enable_shared_from_this<RenderWork> {
  public:
    RenderWork(std::unique_ptr<OfflineRenderSession> session,
               std::unique_ptr<OfflineRenderTask> task, std::shared_ptr<std::atomic_bool> cancelled,
               EngineJobSource::ProgressCallback onProgress,
               EngineJobSource::CompletionCallback onComplete, std::function<void()> onReleased)
        : session_(std::move(session)),
          task_(std::move(task)),
          cancelled_(std::move(cancelled)),
          onProgress_(std::move(onProgress)),
          onComplete_(std::move(onComplete)),
          onReleased_(std::move(onReleased)) {}

    ~RenderWork() {
        cancelAndJoin();
    }

    void start() {
        const auto self = shared_from_this();
        worker_ = std::thread([self] {
            const auto rendered = self->task_->run(
                [cancelled = self->cancelled_] {
                    return cancelled && cancelled->load(std::memory_order_acquire);
                },
                [progress = self->onProgress_](float value) {
                    if (progress)
                        progress(value);
                });
            const auto cancelled =
                self->cancelled_ && self->cancelled_->load(std::memory_order_acquire);
            const EngineJobResult result{cancelled || rendered.error.equalsIgnoreCase("cancelled")
                                             ? EngineJobResultStatus::Cancelled
                                         : rendered.success ? EngineJobResultStatus::Succeeded
                                                            : EngineJobResultStatus::Failed};
            {
                const std::scoped_lock lock(self->mutex_);
                self->result_ = result;
            }
            const std::weak_ptr<RenderWork> weak = self;
            const auto deliver = [weak] {
                if (const auto work = weak.lock())
                    work->deliver();
            };
            if (juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
                !juce::MessageManager::callAsync(deliver))
                deliver();
        });
    }

    void cancelAndJoin() {
        if (cancelled_)
            cancelled_->store(true, std::memory_order_release);
        if (!worker_.joinable())
            return;
        if (worker_.get_id() == std::this_thread::get_id())
            worker_.detach();
        else
            worker_.join();
    }

    void abandon() {
        cancelAndJoin();
        const std::scoped_lock lock(mutex_);
        delivered_ = true;
        task_.reset();
        session_.reset();
        onComplete_ = {};
        onReleased_ = {};
    }

  private:
    void deliver() {
        EngineJobSource::CompletionCallback complete;
        std::function<void()> released;
        EngineJobResult result;
        {
            const std::scoped_lock lock(mutex_);
            if (delivered_ || !result_)
                return;
            delivered_ = true;
            result = *result_;
            task_.reset();
            session_.reset();
            complete = std::move(onComplete_);
            released = std::move(onReleased_);
        }
        if (complete)
            complete(result);
        if (released)
            released();
    }

    std::unique_ptr<OfflineRenderSession> session_;
    std::unique_ptr<OfflineRenderTask> task_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    EngineJobSource::ProgressCallback onProgress_;
    EngineJobSource::CompletionCallback onComplete_;
    std::function<void()> onReleased_;
    std::thread worker_;
    std::mutex mutex_;
    std::optional<EngineJobResult> result_;
    bool delivered_ = false;
};

class LiveEngineJobSource final : public EngineJobSource,
                                  public std::enable_shared_from_this<LiveEngineJobSource> {
  public:
    explicit LiveEngineJobSource(AudioEngine& engine) : engine_(engine) {}

    ~LiveEngineJobSource() override {
        shutdown();
    }

    EngineJobStartStatus renderRange(OfflineRenderRequest request,
                                     std::shared_ptr<std::atomic_bool> cancelled,
                                     ProgressCallback onProgress,
                                     CompletionCallback onComplete) override {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return EngineJobStartStatus::Unavailable;
        if (render_ != nullptr || capture_.active)
            return EngineJobStartStatus::Busy;
        auto session = engine_.createOfflineRenderSession(false);
        if (session == nullptr)
            return EngineJobStartStatus::Unavailable;
        auto task = session->createTask(request);
        if (task == nullptr)
            return EngineJobStartStatus::Failed;

        const std::weak_ptr<LiveEngineJobSource> weak = shared_from_this();
        render_ =
            std::make_shared<RenderWork>(std::move(session), std::move(task), std::move(cancelled),
                                         std::move(onProgress), std::move(onComplete), [weak] {
                                             if (const auto source = weak.lock()) {
                                                 const std::scoped_lock sourceLock(source->mutex_);
                                                 source->render_.reset();
                                             }
                                         });
        render_->start();
        return EngineJobStartStatus::Started;
    }

    EngineJobStartStatus startMasterCapture(const juce::String& jobId,
                                            const juce::String& ownerClientId,
                                            const MasterCaptureRequest& request) override {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return EngineJobStartStatus::Unavailable;
        if (capture_.active || render_ != nullptr)
            return EngineJobStartStatus::Busy;
        auto temporary = std::make_shared<juce::TemporaryFile>(request.destination);
        auto internalRequest = request;
        internalRequest.destination = temporary->getFile();
        const auto started = engine_.startMasterCapture(internalRequest);
        switch (started) {
            case MasterCaptureStartStatus::Started:
                captureFile_ = std::move(temporary);
                captureRequest_ = request;
                capture_ = {.supported = true,
                            .active = true,
                            .failed = false,
                            .jobId = jobId,
                            .ownerClientId = ownerClientId,
                            .format = request.format};
                return EngineJobStartStatus::Started;
            case MasterCaptureStartStatus::Unsupported:
                return EngineJobStartStatus::Unsupported;
            case MasterCaptureStartStatus::Busy:
                return EngineJobStartStatus::Busy;
            case MasterCaptureStartStatus::Unavailable:
                return EngineJobStartStatus::Unavailable;
            case MasterCaptureStartStatus::Failed:
                return EngineJobStartStatus::Failed;
        }
        return EngineJobStartStatus::Failed;
    }

    MasterCaptureResult stopMasterCapture(const juce::String& jobId) override {
        const std::scoped_lock lock(mutex_);
        if (!capture_.active || capture_.jobId != jobId)
            return {false, "Master capture is not active"};
        auto result = engine_.stopMasterCapture();
        if (result.success && captureFile_ != nullptr &&
            captureFile_->getTargetFile().existsAsFile() && !captureRequest_.overwriteExisting)
            result = {false, "Master capture destination now exists"};
        if (result.success &&
            (captureFile_ == nullptr || !captureFile_->overwriteTargetFileWithTemporary()))
            result = {false, "Master capture could not be finalised"};
        captureFile_.reset();
        captureRequest_ = {};
        capture_ = {.supported = engine_.masterCaptureState().supported};
        return result;
    }

    void cancelMasterCapture(const juce::String& jobId) override {
        const std::scoped_lock lock(mutex_);
        if (!capture_.active || capture_.jobId != jobId)
            return;
        engine_.cancelMasterCapture();
        captureFile_.reset();
        captureRequest_ = {};
        capture_ = {.supported = engine_.masterCaptureState().supported};
    }

    MasterCaptureSnapshot masterCaptureStatus() const override {
        const std::scoped_lock lock(mutex_);
        auto result = capture_;
        const auto engineState = engine_.masterCaptureState();
        result.supported = engineState.supported;
        result.active = result.active && engineState.active;
        result.failed = engineState.failed;
        return result;
    }

    void shutdown() override {
        std::shared_ptr<RenderWork> render;
        {
            const std::scoped_lock lock(mutex_);
            if (shutdown_)
                return;
            shutdown_ = true;
            render = render_;
            if (capture_.active)
                engine_.cancelMasterCapture();
            captureFile_.reset();
            captureRequest_ = {};
            capture_ = {};
        }
        if (render != nullptr)
            render->abandon();
        const std::scoped_lock lock(mutex_);
        render_.reset();
    }

  private:
    AudioEngine& engine_;
    mutable std::mutex mutex_;
    std::shared_ptr<RenderWork> render_;
    std::shared_ptr<juce::TemporaryFile> captureFile_;
    MasterCaptureRequest captureRequest_;
    MasterCaptureSnapshot capture_;
    bool shutdown_ = false;
};

}  // namespace

std::shared_ptr<EngineJobSource> makeLiveEngineJobSource(AudioEngine& engine) {
    return std::make_shared<LiveEngineJobSource>(engine);
}

}  // namespace magda::remote
