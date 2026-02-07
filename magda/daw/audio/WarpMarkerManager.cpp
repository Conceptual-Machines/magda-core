#include "WarpMarkerManager.hpp"

#include "../core/ClipManager.hpp"
#include "AudioThumbnailManager.hpp"

namespace magda {

namespace {
// Helper to find WaveAudioClip from MAGDA clip ID
te::WaveAudioClip* findWaveAudioClip(te::Edit& edit,
                                     const std::map<ClipId, std::string>& clipIdToEngineId,
                                     ClipId clipId) {
    auto it = clipIdToEngineId.find(clipId);
    if (it == clipIdToEngineId.end())
        return nullptr;

    const auto& engineId = it->second;
    for (auto* track : te::getAudioTracks(edit)) {
        for (auto* teClip : track->getClips()) {
            if (teClip->itemID.toString().toStdString() == engineId) {
                return dynamic_cast<te::WaveAudioClip*>(teClip);
            }
        }
    }
    return nullptr;
}
}  // namespace

bool WarpMarkerManager::getTransientTimes(te::Edit& edit,
                                          const std::map<ClipId, std::string>& clipIdToEngineId,
                                          ClipId clipId) {
    // Get clip info for file path
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->type != ClipType::Audio || clip->audioFilePath.isEmpty()) {
        return false;
    }

    // Check cache first
    auto& thumbnailManager = AudioThumbnailManager::getInstance();
    if (thumbnailManager.getCachedTransients(clip->audioFilePath) != nullptr) {
        return true;
    }

    // Find TE WaveAudioClip via clipIdToEngineId
    auto it = clipIdToEngineId.find(clipId);
    if (it == clipIdToEngineId.end()) {
        return false;
    }

    std::string engineId = it->second;
    te::WaveAudioClip* audioClipPtr = nullptr;

    for (auto* track : te::getAudioTracks(edit)) {
        for (auto* teClip : track->getClips()) {
            if (teClip->itemID.toString().toStdString() == engineId) {
                audioClipPtr = dynamic_cast<te::WaveAudioClip*>(teClip);
                break;
            }
        }
        if (audioClipPtr)
            break;
    }

    if (!audioClipPtr) {
        return false;
    }

    // Get WarpTimeManager from the clip
    auto& warpManager = audioClipPtr->getWarpTimeManager();

    // Trigger detection if not started
    warpManager.editFinishedLoading();

    // Poll for completion
    auto [complete, transientPositions] = warpManager.getTransientTimes();

    if (complete) {
        // Convert TimePosition array to double array
        juce::Array<double> times;
        times.ensureStorageAllocated(transientPositions.size());
        for (const auto& tp : transientPositions) {
            times.add(tp.inSeconds());
        }

        thumbnailManager.cacheTransients(clip->audioFilePath, times);
        DBG("WarpMarkerManager: Cached " << times.size() << " transients for "
                                         << clip->audioFilePath);
        return true;
    }

    return false;
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
    double clipOffset = clip->offset;

    // Get cached transients from AudioThumbnailManager
    auto* cachedTransients =
        AudioThumbnailManager::getInstance().getCachedTransients(clip->audioFilePath);
    DBG("WarpMarkerManager::enableWarp cachedTransients="
        << (cachedTransients ? juce::String(cachedTransients->size()) : "null")
        << " file=" << clip->audioFilePath << " offset=" << clipOffset);
    if (cachedTransients) {
        // Insert identity-mapped markers at each transient position within the visible range
        double visibleEnd = clipOffset + clip->length * clip->speedRatio;
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
