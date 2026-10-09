#include "TransientDetection.hpp"

#include <juce_events/juce_events.h>

#include <map>

#include "../core/ClipManager.hpp"
#include "../engine/TracktionFork.hpp"
#include "AudioThumbnailManager.hpp"

#if MAGDA_HAS_NATIVE_ENGINE
    #include "../engine/host/EngineHost.hpp"
#endif

namespace magda::transients {

namespace {

#if MAGDA_HAS_NATIVE_ENGINE
/// Native detection, one file at a time on a worker, cached per source file.
class NativeDetector final : private juce::Timer {
  public:
    static NativeDetector& getInstance() {
        static NativeDetector detector;
        return detector;
    }

    ~NativeDetector() override {
        stopTimer();
        workers_.removeAllJobs(true, -1);
    }

    bool detect(ClipId clipId) {
        const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
        if (event == nullptr || event->sourceFilePath().isEmpty())
            return false;
        if (pending_.contains(clipId))
            return false;
        if (AudioThumbnailManager::getInstance().getCachedTransients(event->sourceFilePath()))
            return true;
        if (!active_.contains(event->sourceFilePath()))
            start(event->sourceFilePath(), kDefaultSensitivity);
        return false;
    }

    void setSensitivity(ClipId clipId, float sensitivity) {
        pending_[clipId] = juce::jlimit(0.0f, 1.0f, sensitivity);
        // A drag sends many values; only the settled one is detected.
        startTimer(150);
    }

  private:
    /// The fork's WarpTimeManager default, so both engines seed the same markers.
    static constexpr float kDefaultSensitivity = 0.5f;

    NativeDetector() = default;

    void timerCallback() override {
        stopTimer();
        auto pending = std::move(pending_);
        pending_.clear();
        for (const auto& [clipId, sensitivity] : pending)
            if (const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId)))
                start(event->sourceFilePath(), sensitivity);
    }

    void start(const juce::String& path, float sensitivity) {
        if (path.isEmpty())
            return;
        const auto generation = ++generation_;
        active_[path] = generation;
        AudioThumbnailManager::getInstance().clearCachedTransients(path);

        juce::WeakReference<NativeDetector> weakThis(this);
        workers_.addJob([weakThis, path, sensitivity, generation] {
            const auto detected =
                daw::engine_host::EngineHost::detectSourceTransients(path, sensitivity);
            juce::MessageManager::callAsync([weakThis, path, generation, detected] {
                auto* self = weakThis.get();
                if (self == nullptr)
                    return;
                const auto found = self->active_.find(path);
                if (found == self->active_.end() || found->second != generation)
                    return;
                self->active_.erase(found);
                juce::Array<double> times;
                times.addArray(detected.data(), static_cast<int>(detected.size()));
                AudioThumbnailManager::getInstance().cacheTransients(path, times);
            });
        });
    }

    juce::ThreadPool workers_{1};
    std::map<ClipId, float> pending_;
    std::map<juce::String, std::uint64_t> active_;
    std::uint64_t generation_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(NativeDetector)
};
#endif

}  // namespace

bool detect(ClipId clipId) {
    if (tracktion_fork::isRendering())
        return tracktion_fork::detectTransients(clipId);
#if MAGDA_HAS_NATIVE_ENGINE
    return NativeDetector::getInstance().detect(clipId);
#else
    return false;
#endif
}

void setSensitivity(ClipId clipId, float sensitivity) {
    if (tracktion_fork::isRendering()) {
        tracktion_fork::setTransientSensitivity(clipId, sensitivity);
        return;
    }
#if MAGDA_HAS_NATIVE_ENGINE
    NativeDetector::getInstance().setSensitivity(clipId, sensitivity);
#endif
}

}  // namespace magda::transients
