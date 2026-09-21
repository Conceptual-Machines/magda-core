#include "WarpMarkerManager.hpp"

#include <juce_events/juce_events.h>

#include "../core/ClipManager.hpp"
#include "AudioThumbnailManager.hpp"

namespace magda {

namespace {
// Helper to find WaveAudioClip from a TE engine ID.
// Searches both arrangement clips on the timeline and session clips in slots.
te::WaveAudioClip* findWaveAudioClipByEngineId(te::Edit& edit, const std::string& engineId) {
    if (engineId.empty())
        return nullptr;
    for (auto* track : te::getAudioTracks(edit)) {
        // Search arrangement clips on the timeline
        for (auto* teClip : track->getClips()) {
            if (teClip->itemID.toString().toStdString() == engineId) {
                return dynamic_cast<te::WaveAudioClip*>(teClip);
            }
        }
        // Search session clips in clip slots
        for (auto* slot : track->getClipSlotList().getClipSlots()) {
            if (auto* teClip = slot->getClip()) {
                if (teClip->itemID.toString().toStdString() == engineId) {
                    return dynamic_cast<te::WaveAudioClip*>(teClip);
                }
            }
        }
    }
    return nullptr;
}

// Convenience wrapper: resolve via the caller's clipIdToEngineId map.
te::WaveAudioClip* findWaveAudioClip(te::Edit& edit,
                                     const std::map<ClipId, std::string>& clipIdToEngineId,
                                     ClipId clipId) {
    auto it = clipIdToEngineId.find(clipId);
    if (it == clipIdToEngineId.end())
        return nullptr;
    return findWaveAudioClipByEngineId(edit, it->second);
}

}  // namespace

WarpMarkerManager::~WarpMarkerManager() {
    stopTimer();
    for (auto& [_, active] : activeDetections_) {
        if (active.warpManager != nullptr)
            active.warpManager->removeListener(this);
    }
}

void WarpMarkerManager::setTransientSensitivity(
    te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId, ClipId clipId,
    float sensitivity) {
    // Resolve the engineId at queue time (a single map lookup) instead
    // of snapshotting the whole clipIdToEngineId map per call.
    auto& pending = pendingDetections_[clipId];
    pending.sensitivity = sensitivity;
    auto it = clipIdToEngineId.find(clipId);
    pending.engineId = (it != clipIdToEngineId.end()) ? it->second : std::string{};
    pending.edit = &edit;

    // A drag can generate dozens of values per second. Restart the quiet-period
    // timer so only the final value clears the cache and launches detection.
    startTimer(150);
}

void WarpMarkerManager::timerCallback() {
    stopTimer();

    auto pending = std::move(pendingDetections_);
    pendingDetections_.clear();

    for (const auto& [clipId, detection] : pending) {
        if (detection.edit != nullptr && !detection.engineId.empty())
            applySensitivityNow(*detection.edit, detection.engineId, clipId, detection.sensitivity);
    }
}

void WarpMarkerManager::applySensitivityNow(te::Edit& edit, const std::string& engineId,
                                            ClipId clipId, float sensitivity) {
    startDetection(edit, engineId, clipId, sensitivity);
}

bool WarpMarkerManager::startDetection(te::Edit& edit, const std::string& engineId, ClipId clipId,
                                       std::optional<float> sensitivity) {
    const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
    if (event == nullptr || event->sourceFilePath().isEmpty())
        return false;
    const auto sourcePath = event->sourceFilePath();

    te::WaveAudioClip* audioClipPtr = findWaveAudioClipByEngineId(edit, engineId);
    if (!audioClipPtr)
        return false;

    auto& warpManager = audioClipPtr->getWarpTimeManager();
    if (sensitivity.has_value())
        warpManager.setTransientSensitivity(*sensitivity);

    auto active = activeDetections_.find(clipId);
    if (active != activeDetections_.end() && active->second.warpManager != nullptr) {
        active->second.warpManager->removeListener(this);
        clipByWarpManager_.erase(active->second.warpManager);
    }

    warpManager.addListener(this);
    detectionInFlight_.insert(clipId);
    activeDetections_[clipId] = {sourcePath, te::WarpTimeManager::Ptr(&warpManager)};
    clipByWarpManager_[&warpManager] = clipId;

    // Clear cache so listeners know the displayed transients are stale.
    AudioThumbnailManager::getInstance().clearCachedTransients(sourcePath);

    warpManager.detectTransients();

    return true;
}

bool WarpMarkerManager::getTransientTimes(te::Edit& edit,
                                          const std::map<ClipId, std::string>& clipIdToEngineId,
                                          ClipId clipId) {
    // Get clip info for file path
    const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
    if (event == nullptr || event->sourceFilePath().isEmpty())
        return false;

    // Check cache first
    auto& thumbnailManager = AudioThumbnailManager::getInstance();
    if (thumbnailManager.getCachedTransients(event->sourceFilePath()) != nullptr)
        return true;

    if (pendingDetections_.count(clipId))
        return false;

    if (detectionInFlight_.count(clipId))
        return false;

    // Find TE WaveAudioClip via shared helper
    te::WaveAudioClip* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr)
        return false;

    startDetection(edit, clipIdToEngineId.at(clipId), clipId, std::nullopt);
    return false;
}

void WarpMarkerManager::transientDetectionFinished(te::WarpTimeManager& warpManager,
                                                   bool completedOk) {
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        juce::WeakReference<WarpMarkerManager> weakThis(this);
        juce::MessageManager::callAsync([weakThis, warpManagerPtr = &warpManager, completedOk]() {
            if (auto* self = weakThis.get(); self != nullptr && warpManagerPtr != nullptr)
                self->transientDetectionFinished(*warpManagerPtr, completedOk);
        });
        return;
    }

    auto it = clipByWarpManager_.find(&warpManager);
    if (it == clipByWarpManager_.end())
        return;

    finishDetection(it->second, warpManager, completedOk);
}

void WarpMarkerManager::finishDetection(ClipId clipId, te::WarpTimeManager& warpManager,
                                        bool completedOk) {
    const bool replacementPending = pendingDetections_.count(clipId) != 0;
    warpManager.removeListener(this);
    clipByWarpManager_.erase(&warpManager);
    detectionInFlight_.erase(clipId);

    auto active = activeDetections_.find(clipId);
    const juce::String filePath =
        active != activeDetections_.end() ? active->second.filePath : juce::String();
    activeDetections_.erase(clipId);

    auto [complete, transientPositions] = warpManager.getTransientTimes();
    if (replacementPending || !completedOk || !complete || filePath.isEmpty())
        return;

    juce::Array<double> times;
    times.ensureStorageAllocated(transientPositions.size());
    for (const auto& tp : transientPositions) {
        times.add(tp.inSeconds());
    }

    AudioThumbnailManager::getInstance().cacheTransients(filePath, times);
}

}  // namespace magda
