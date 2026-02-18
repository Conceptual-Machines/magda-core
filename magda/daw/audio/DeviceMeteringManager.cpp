#include "DeviceMeteringManager.hpp"

#include "PluginManager.hpp"

namespace magda {

// Static members
std::map<te::Edit*, DeviceMeteringManager*> DeviceMeteringManager::editMap_;
juce::CriticalSection DeviceMeteringManager::editMapLock_;

te::LevelMeasurer& DeviceMeteringManager::getOrCreateMeasurer(DeviceId deviceId) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(deviceId);
    if (it != entries_.end())
        return it->second->measurer;

    auto entry = std::make_unique<Entry>();
    entry->measurer.addClient(entry->client);
    entry->clientRegistered = true;
    auto& measurer = entry->measurer;
    entries_[deviceId] = std::move(entry);
    return measurer;
}

void DeviceMeteringManager::removeMeasurer(DeviceId deviceId) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(deviceId);
    if (it != entries_.end()) {
        if (it->second->clientRegistered)
            it->second->measurer.removeClient(it->second->client);
        entries_.erase(it);
    }
}

DeviceId DeviceMeteringManager::getDeviceIdForPlugin(te::Plugin* plugin) const {
    if (!pluginManager_ || !plugin)
        return INVALID_DEVICE_ID;

    return pluginManager_->getDeviceIdForPlugin(plugin);
}

void DeviceMeteringManager::updateAllClients() {
    juce::ScopedLock sl(lock_);
    for (auto& [deviceId, entry] : entries_) {
        if (!entry->clientRegistered)
            continue;

        auto levelL = entry->client.getAndClearAudioLevel(0);
        auto levelR = entry->client.getAndClearAudioLevel(1);

        float peakL = juce::Decibels::decibelsToGain(levelL.dB);
        float peakR = juce::Decibels::decibelsToGain(levelR.dB);

        entry->peakL.store(peakL, std::memory_order_relaxed);
        entry->peakR.store(peakR, std::memory_order_relaxed);
    }
}

bool DeviceMeteringManager::getLatestLevels(DeviceId deviceId, DeviceMeterData& out) const {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(deviceId);
    if (it == entries_.end())
        return false;

    out.peakL = it->second->peakL.load(std::memory_order_relaxed);
    out.peakR = it->second->peakR.load(std::memory_order_relaxed);
    return true;
}

void DeviceMeteringManager::setGain(DeviceId deviceId, float gain) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(deviceId);
    if (it != entries_.end())
        it->second->gainLinear.store(gain, std::memory_order_relaxed);
}

std::atomic<float>* DeviceMeteringManager::getGainAtomic(DeviceId deviceId) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(deviceId);
    if (it != entries_.end())
        return &it->second->gainLinear;
    return nullptr;
}

void DeviceMeteringManager::clear() {
    juce::ScopedLock sl(lock_);
    for (auto& [deviceId, entry] : entries_) {
        if (entry->clientRegistered)
            entry->measurer.removeClient(entry->client);
    }
    entries_.clear();
}

DeviceMeteringManager* DeviceMeteringManager::getInstanceForEdit(te::Edit& edit) {
    juce::ScopedLock sl(editMapLock_);
    auto it = editMap_.find(&edit);
    return it != editMap_.end() ? it->second : nullptr;
}

void DeviceMeteringManager::registerForEdit(te::Edit& edit, DeviceMeteringManager* mgr) {
    juce::ScopedLock sl(editMapLock_);
    editMap_[&edit] = mgr;
}

void DeviceMeteringManager::unregisterForEdit(te::Edit& edit) {
    juce::ScopedLock sl(editMapLock_);
    editMap_.erase(&edit);
}

}  // namespace magda
