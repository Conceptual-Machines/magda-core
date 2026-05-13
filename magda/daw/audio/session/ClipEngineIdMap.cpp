#include "session/ClipEngineIdMap.hpp"

namespace magda {

bool ClipEngineIdMap::contains(ClipId clipId) const {
    juce::ScopedLock lock(lock_);
    return clipIdToEngineId_.find(clipId) != clipIdToEngineId_.end();
}

std::optional<std::string> ClipEngineIdMap::getEngineId(ClipId clipId) const {
    juce::ScopedLock lock(lock_);
    auto it = clipIdToEngineId_.find(clipId);
    if (it == clipIdToEngineId_.end())
        return std::nullopt;
    return it->second;
}

std::optional<ClipId> ClipEngineIdMap::getClipId(const std::string& engineId) const {
    juce::ScopedLock lock(lock_);
    auto it = engineIdToClipId_.find(engineId);
    if (it == engineIdToClipId_.end())
        return std::nullopt;
    return it->second;
}

void ClipEngineIdMap::set(ClipId clipId, std::string engineId) {
    juce::ScopedLock lock(lock_);

    if (auto existing = clipIdToEngineId_.find(clipId); existing != clipIdToEngineId_.end())
        engineIdToClipId_.erase(existing->second);

    if (auto existing = engineIdToClipId_.find(engineId); existing != engineIdToClipId_.end())
        clipIdToEngineId_.erase(existing->second);

    clipIdToEngineId_[clipId] = engineId;
    engineIdToClipId_[clipIdToEngineId_[clipId]] = clipId;
}

void ClipEngineIdMap::erase(ClipId clipId) {
    juce::ScopedLock lock(lock_);
    auto it = clipIdToEngineId_.find(clipId);
    if (it == clipIdToEngineId_.end())
        return;

    engineIdToClipId_.erase(it->second);
    clipIdToEngineId_.erase(it);
}

ClipEngineIdMap::Snapshot ClipEngineIdMap::snapshot() const {
    juce::ScopedLock lock(lock_);
    return clipIdToEngineId_;
}

}  // namespace magda
