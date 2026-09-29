#include "WarpMarkerManager.hpp"

#include "../core/ClipPropertyCommands.hpp"
#include "../core/UndoManager.hpp"
#include "../engine/host/EngineHost.hpp"
#include "AudioThumbnailManager.hpp"

namespace magda {

WarpMarkerManager& WarpMarkerManager::getInstance() {
    static WarpMarkerManager manager;
    return manager;
}

WarpMarkerManager::WarpMarkerManager() {
    ClipManager::getInstance().addListener(this);
}

WarpMarkerManager::~WarpMarkerManager() {
    ClipManager::getInstance().removeListener(this);
    stopBackgroundWork();
}

void WarpMarkerManager::stopBackgroundWork() {
    stopTimer();
    pending_.clear();
    active_.clear();
    detectedWith_.clear();
    ++generation_;
    workers_.removeAllJobs(true, -1);
}

bool WarpMarkerManager::getTransientTimes(ClipId clipId) {
    const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
    if (event == nullptr || event->sourceFilePath().isEmpty())
        return false;
    if (pending_.contains(clipId))
        return false;
    if (AudioThumbnailManager::getInstance().getCachedTransients(event->sourceFilePath()))
        return true;
    if (!active_.contains(event->sourceFilePath()))
        startDetection(clipId);
    return false;
}

void WarpMarkerManager::setTransientSensitivity(ClipId clipId, float sensitivity) {
    pending_[clipId] = juce::jlimit(0.0f, 1.0f, sensitivity);
    // Invalidate an earlier result before the slider's quiet period finishes.
    if (const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId))) {
        active_.erase(event->sourceFilePath());
        detectedWith_.erase(event->sourceFilePath());
        AudioThumbnailManager::getInstance().clearCachedTransients(event->sourceFilePath());
    }
    startTimer(150);
}

void WarpMarkerManager::timerCallback() {
    stopTimer();
    auto pending = std::move(pending_);
    pending_.clear();
    for (const auto& [clipId, sensitivity] : pending) {
        const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
        if (event == nullptr)
            continue;
        const auto path = event->sourceFilePath();
        if (event->beatSensitivity != sensitivity)
            UndoManager::getInstance().executeCommand(std::make_unique<SetClipPropertyCommand>(
                clipId, "Set Transient Sensitivity",
                [sensitivity](ClipManager& manager, ClipId id) {
                    manager.setBeatSensitivity(id, sensitivity);
                }));
        if (!active_.contains(path))
            startDetection(clipId);
    }
}

void WarpMarkerManager::clipPropertyChanged(ClipId clipId) {
    if (pending_.contains(clipId))
        return;
    const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
    if (event == nullptr || event->sourceFilePath().isEmpty())
        return;
    const auto found = detectedWith_.find(event->sourceFilePath());
    if (found == detectedWith_.end() || found->second == event->beatSensitivity)
        return;
    startDetection(clipId);
}

void WarpMarkerManager::startDetection(ClipId clipId) {
    const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
    if (event == nullptr || event->sourceFilePath().isEmpty())
        return;
    const auto path = event->sourceFilePath();
    const auto sensitivity = event->beatSensitivity;
    const auto generation = ++generation_;
    active_[path] = generation;
    detectedWith_[path] = sensitivity;
    AudioThumbnailManager::getInstance().clearCachedTransients(path);

    juce::WeakReference<WarpMarkerManager> weakThis(this);
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

}  // namespace magda
