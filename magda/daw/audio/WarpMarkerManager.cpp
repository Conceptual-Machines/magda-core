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
    PendingDetection det;
    det.sensitivity = sensitivity;
    auto it = clipIdToEngineId.find(clipId);
    det.engineId = (it != clipIdToEngineId.end()) ? it->second : std::string{};
    det.edit = &edit;

    // Restart immediately so the latest sensitivity doesn't wait behind an old
    // detection job. WarpTimeManager::detectTransients detaches/cancels the
    // previous job before submitting the replacement.
    applySensitivityNow(edit, det.engineId, clipId, sensitivity);
}

void WarpMarkerManager::applySensitivityNow(te::Edit& edit, const std::string& engineId,
                                            ClipId clipId, float sensitivity) {
    startDetection(edit, engineId, clipId, sensitivity);
}

bool WarpMarkerManager::startDetection(te::Edit& edit, const std::string& engineId, ClipId clipId,
                                       std::optional<float> sensitivity) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return false;
    if (!clip->isAudio())
        return false;
    if (clip->audio().source.filePath.isEmpty())
        return false;

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
    activeDetections_[clipId] = {clip->audio().source.filePath,
                                 te::WarpTimeManager::Ptr(&warpManager)};
    clipByWarpManager_[&warpManager] = clipId;

    // Clear cache so listeners know the displayed transients are stale.
    AudioThumbnailManager::getInstance().clearCachedTransients(clip->audio().source.filePath);

    warpManager.detectTransients();

    return true;
}

bool WarpMarkerManager::getTransientTimes(te::Edit& edit,
                                          const std::map<ClipId, std::string>& clipIdToEngineId,
                                          ClipId clipId) {
    // Get clip info for file path
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || !clip->isAudio() || clip->audio().source.filePath.isEmpty())
        return false;

    // Check cache first
    auto& thumbnailManager = AudioThumbnailManager::getInstance();
    if (thumbnailManager.getCachedTransients(clip->audio().source.filePath) != nullptr)
        return true;

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
        juce::MessageManager::callAsync([this, warpManagerPtr = &warpManager, completedOk]() {
            if (warpManagerPtr != nullptr)
                transientDetectionFinished(*warpManagerPtr, completedOk);
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
    warpManager.removeListener(this);
    clipByWarpManager_.erase(&warpManager);
    detectionInFlight_.erase(clipId);

    auto active = activeDetections_.find(clipId);
    const juce::String filePath =
        active != activeDetections_.end() ? active->second.filePath : juce::String();
    activeDetections_.erase(clipId);

    auto [complete, transientPositions] = warpManager.getTransientTimes();
    if (!completedOk || !complete || filePath.isEmpty())
        return;

    juce::Array<double> times;
    times.ensureStorageAllocated(transientPositions.size());
    for (const auto& tp : transientPositions) {
        times.add(tp.inSeconds());
    }

    AudioThumbnailManager::getInstance().cacheTransients(filePath, times);
}

void WarpMarkerManager::enableWarp(te::Edit& edit,
                                   const std::map<ClipId, std::string>& clipIdToEngineId,
                                   ClipId clipId) {
    auto* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr)
        return;

    auto& warpManager = audioClipPtr->getWarpTimeManager();

    // Remove any existing markers (creates default boundaries at 0 and sourceLen)
    warpManager.removeAllMarkers();

    // Get clip info
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    // Get the clip's offset - this is where playback starts in the source file
    double clipOffset = clip->getSourceOffset();

    // Get cached transients from AudioThumbnailManager
    auto* cachedTransients =
        AudioThumbnailManager::getInstance().getCachedTransients(clip->audio().source.filePath);
    DBG("WarpMarkerManager::enableWarp cachedTransients="
        << (cachedTransients ? juce::String(cachedTransients->size()) : "null")
        << " file=" << clip->audio().source.filePath << " offset=" << clipOffset);
    if (cachedTransients) {
        // Insert identity-mapped markers at each transient position within the visible range
        double bpm = edit.tempoSequence.getBpmAt(te::TimePosition());
        if (bpm <= 0.0)
            bpm = 120.0;
        double visibleEnd = clipOffset + clip->timelineToSource(clip->getTimelineLength(bpm));
        for (double t : *cachedTransients) {
            // Only include transients within the visible portion of the clip
            if (t >= clipOffset && t <= visibleEnd) {
                auto pos = te::TimePosition::fromSeconds(t);
                warpManager.insertMarker(te::WarpMarker(pos, pos));
            }
        }
    }

    // Set end marker to source length
    auto sourceLen = warpManager.getSourceLength();
    warpManager.setWarpEndMarkerTime(te::TimePosition::fromSeconds(0.0) + sourceLen);

    // Warp requires a valid time stretch mode — TE only auto-upgrades for
    // autoTempo/autoPitch, not for warp-only clips.
    if (audioClipPtr->getTimeStretchMode() == te::TimeStretcher::disabled) {
        audioClipPtr->setTimeStretchMode(te::TimeStretcher::defaultMode);
    }

    audioClipPtr->setWarpTime(true);

    DBG("WarpMarkerManager::enableWarp clip " << clipId << " -> " << warpManager.getMarkers().size()
                                              << " markers");
}

void WarpMarkerManager::disableWarp(te::Edit& edit,
                                    const std::map<ClipId, std::string>& clipIdToEngineId,
                                    ClipId clipId) {
    auto* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr)
        return;

    auto& warpManager = audioClipPtr->getWarpTimeManager();
    warpManager.removeAllMarkers();
    audioClipPtr->setWarpTime(false);

    DBG("WarpMarkerManager::disableWarp clip " << clipId);
}

std::vector<WarpMarkerInfo> WarpMarkerManager::getWarpMarkers(
    te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId, ClipId clipId) {
    std::vector<WarpMarkerInfo> result;

    auto* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr) {
        DBG("WarpMarkerManager::getWarpMarkers clip " << clipId << " -> no TE clip found");
        return result;
    }

    auto& warpManager = audioClipPtr->getWarpTimeManager();
    const auto& markers = warpManager.getMarkers();

    // Return ALL markers including TE's boundary markers at (0,0) and (sourceLen,sourceLen).
    // The visual renderer needs the same boundaries as the audio engine for correct interpolation.
    int count = markers.size();
    result.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto* marker = markers.getUnchecked(i);
        result.push_back({marker->sourceTime.inSeconds(), marker->warpTime.inSeconds()});
    }

    return result;
}

int WarpMarkerManager::addWarpMarker(te::Edit& edit,
                                     const std::map<ClipId, std::string>& clipIdToEngineId,
                                     ClipId clipId, double sourceTime, double warpTime) {
    auto* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr) {
        DBG("WarpMarkerManager::addWarpMarker - clip not found");
        return -1;
    }

    auto& warpManager = audioClipPtr->getWarpTimeManager();
    int markerCountBefore = warpManager.getMarkers().size();

    int teIndex = warpManager.insertMarker(te::WarpMarker(te::TimePosition::fromSeconds(sourceTime),
                                                          te::TimePosition::fromSeconds(warpTime)));

    int markerCountAfter = warpManager.getMarkers().size();
    DBG("WarpMarkerManager::addWarpMarker clip "
        << clipId << " src=" << sourceTime << " warp=" << warpTime << " -> teIndex=" << teIndex
        << " (markers: " << markerCountBefore << " -> " << markerCountAfter << ")");

    // Return TE index directly - UI now uses the same index space
    return teIndex;
}

double WarpMarkerManager::moveWarpMarker(te::Edit& edit,
                                         const std::map<ClipId, std::string>& clipIdToEngineId,
                                         ClipId clipId, int index, double newWarpTime) {
    auto* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr)
        return newWarpTime;

    // Use TE index directly - UI now uses the same index space
    auto& warpManager = audioClipPtr->getWarpTimeManager();
    auto result = warpManager.moveMarker(index, te::TimePosition::fromSeconds(newWarpTime));
    return result.inSeconds();
}

void WarpMarkerManager::removeWarpMarker(te::Edit& edit,
                                         const std::map<ClipId, std::string>& clipIdToEngineId,
                                         ClipId clipId, int index) {
    auto* audioClipPtr = findWaveAudioClip(edit, clipIdToEngineId, clipId);
    if (!audioClipPtr)
        return;

    // Use TE index directly - UI now uses the same index space
    auto& warpManager = audioClipPtr->getWarpTimeManager();
    warpManager.removeMarker(index);
}

}  // namespace magda
