#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "../engine/AudioEngine.hpp"

namespace magda::remote {

enum class EngineJobStartStatus { Started, Busy, Unsupported, Unavailable, Failed };
enum class EngineJobResultStatus { Succeeded, Cancelled, Failed };

struct EngineJobResult {
    EngineJobResultStatus status = EngineJobResultStatus::Failed;
};

struct MasterCaptureSnapshot {
    bool supported = false;
    bool active = false;
    bool failed = false;
    juce::String jobId;
    juce::String ownerClientId;
    OfflineRenderFormat format = OfflineRenderFormat::Wav;
};

class EngineJobSource {
  public:
    using ProgressCallback = std::function<void(double)>;
    using CompletionCallback = std::function<void(EngineJobResult)>;

    virtual ~EngineJobSource() = default;

    virtual EngineJobStartStatus renderRange(OfflineRenderRequest request,
                                             std::shared_ptr<std::atomic_bool> cancelled,
                                             ProgressCallback onProgress,
                                             CompletionCallback onComplete) = 0;
    virtual EngineJobStartStatus startMasterCapture(const juce::String& jobId,
                                                    const juce::String& ownerClientId,
                                                    const MasterCaptureRequest& request) = 0;
    virtual MasterCaptureResult stopMasterCapture(const juce::String& jobId) = 0;
    virtual void cancelMasterCapture(const juce::String& jobId) = 0;
    virtual MasterCaptureSnapshot masterCaptureStatus() const = 0;
    virtual void shutdown() = 0;
};

std::shared_ptr<EngineJobSource> makeLiveEngineJobSource(AudioEngine& engine);

}  // namespace magda::remote
