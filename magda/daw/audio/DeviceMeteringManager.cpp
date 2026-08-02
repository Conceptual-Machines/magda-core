#include "DeviceMeteringManager.hpp"

#include <tracktion_graph/tracktion_graph.h>

// TE's internal node headers require the graph module definitions first.
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <tracktion_engine/playback/graph/tracktion_LevelMeasuringNode.h>

#include <mutex>

#include "plugin_manager/PluginManager.hpp"

namespace magda {

namespace {

// Applies the device's metering gain, and owns the tap handle that keeps the
// manager entry alive. The LevelMeasuringNode wrapped around this one holds a
// bare reference to the same entry's measurer, and this node is its input, so
// pinning the entry here covers both for the whole subtree's lifetime.
class DeviceGainNode final : public te::graph::Node {
  public:
    DeviceGainNode(std::unique_ptr<te::graph::Node> inputNode, DeviceMeteringManager::GraphTap tap)
        : input_(std::move(inputNode)), tap_(std::move(tap)), gain_(*tap_.gain) {
        setOptimisations({te::graph::ClearBuffers::no, te::graph::AllocateAudioBuffer::yes});
    }

    te::graph::NodeProperties getNodeProperties() override {
        auto props = input_->getNodeProperties();
        if (props.nodeID != 0)
            tracktion::hash_combine(props.nodeID, static_cast<size_t>(7364928150483726199));
        return props;
    }

    std::vector<te::graph::Node*> getDirectInputNodes() override {
        return {input_.get()};
    }

    bool isReadyToProcess() override {
        return input_->hasProcessed();
    }

    void process(te::graph::Node::ProcessContext& pc) override {
        auto sourceBuffers = input_->getProcessedOutput();
        auto destAudio = pc.buffers.audio;
        jassert(sourceBuffers.audio.getSize() == destAudio.getSize());

        te::graph::copyIfNotAliased(destAudio, sourceBuffers.audio);

        if (input_->numOutputNodes == 1)
            pc.buffers.midi.swapWith(sourceBuffers.midi);
        else
            pc.buffers.midi.copyFrom(sourceBuffers.midi);

        const auto gain = gain_.load(std::memory_order_relaxed);
        if (gain != 1.0f)
            te::graph::toAudioBuffer(destAudio).applyGain(gain);
    }

  private:
    std::unique_ptr<te::graph::Node> input_;
    DeviceMeteringManager::GraphTap tap_;
    std::atomic<float>& gain_;
};

std::unique_ptr<te::graph::Node> addDeviceMeteringTap(te::Plugin& plugin,
                                                      std::unique_ptr<te::graph::Node> node) {
    auto* manager = DeviceMeteringManager::getInstanceForEdit(plugin.edit);
    if (!manager)
        return node;

    const auto devicePath = manager->getDevicePathForPlugin(&plugin);
    if (!devicePath.isValid())
        return node;

    auto tap = manager->acquireGraphTap(devicePath);
    if (!tap.isValid())
        return node;

    auto& measurer = *tap.measurer;
    node = std::make_unique<DeviceGainNode>(std::move(node), std::move(tap));

    return std::make_unique<te::LevelMeasuringNode>(std::move(node), measurer);
}

void installDeviceMeteringTap() {
    static std::once_flag flag;
    std::call_once(flag,
                   [] { te::EditNodeBuilder::insertOptionalPluginTapNode = addDeviceMeteringTap; });
}

}  // namespace

std::map<te::Edit*, DeviceMeteringManager*> DeviceMeteringManager::editMap_;
juce::CriticalSection DeviceMeteringManager::editMapLock_;

ChainNodePath DeviceMeteringManager::legacyPathForDeviceId(DeviceId deviceId) {
    ChainNodePath path;
    path.topLevelDeviceId = deviceId;
    return path;
}

const std::shared_ptr<DeviceMeteringManager::Entry>& DeviceMeteringManager::ensureEntrySharedLocked(
    const ChainNodePath& devicePath) {
    auto& entry = entries_[devicePath];
    if (!entry)
        entry = std::make_shared<Entry>();
    return entry;
}

DeviceMeteringManager::Entry& DeviceMeteringManager::ensureEntryLocked(
    const ChainNodePath& devicePath) {
    return *ensureEntrySharedLocked(devicePath);
}

te::LevelMeasurer& DeviceMeteringManager::getOrCreateMeasurer(const ChainNodePath& devicePath) {
    juce::ScopedLock sl(lock_);
    auto& entry = ensureEntryLocked(devicePath);
    if (!entry.clientRegistered) {
        entry.measurer.addClient(entry.client);
        entry.clientRegistered = true;
    }
    return entry.measurer;
}

te::LevelMeasurer& DeviceMeteringManager::getOrCreateMeasurer(DeviceId deviceId) {
    return getOrCreateMeasurer(legacyPathForDeviceId(deviceId));
}

void DeviceMeteringManager::removeMeasurer(const ChainNodePath& devicePath) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(devicePath);
    if (it != entries_.end()) {
        if (it->second->clientRegistered)
            it->second->measurer.removeClient(it->second->client);
        entries_.erase(it);
    }
}

void DeviceMeteringManager::removeMeasurer(DeviceId deviceId) {
    removeMeasurer(legacyPathForDeviceId(deviceId));
}

DeviceId DeviceMeteringManager::getDeviceIdForPlugin(te::Plugin* plugin) const {
    if (!pluginManager_ || !plugin)
        return INVALID_DEVICE_ID;

    return pluginManager_->getDeviceIdForPlugin(plugin);
}

ChainNodePath DeviceMeteringManager::getDevicePathForPlugin(te::Plugin* plugin) const {
    if (!pluginManager_ || !plugin)
        return {};

    return pluginManager_->getDevicePathForPlugin(plugin);
}

void DeviceMeteringManager::updateAllClients() {
    juce::ScopedLock sl(lock_);
    for (auto& [devicePath, entry] : entries_) {
        if (!entry->clientRegistered) {
            if (entry->realtimeTap) {
                entry->peakL.store(
                    entry->realtimeTap->peakL.exchange(0.0f, std::memory_order_relaxed),
                    std::memory_order_relaxed);
                entry->peakR.store(
                    entry->realtimeTap->peakR.exchange(0.0f, std::memory_order_relaxed),
                    std::memory_order_relaxed);
            }
            continue;
        }

        auto levelL = entry->client.getAndClearAudioLevel(0);
        auto levelR = entry->client.getAndClearAudioLevel(1);

        float peakL = juce::Decibels::decibelsToGain(levelL.dB);
        float peakR = juce::Decibels::decibelsToGain(levelR.dB);

        entry->peakL.store(peakL, std::memory_order_relaxed);
        entry->peakR.store(peakR, std::memory_order_relaxed);
    }
}

bool DeviceMeteringManager::getLatestLevels(const ChainNodePath& devicePath,
                                            DeviceMeterData& out) const {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(devicePath);
    if (it == entries_.end())
        return false;

    out.peakL = it->second->peakL.load(std::memory_order_relaxed);
    out.peakR = it->second->peakR.load(std::memory_order_relaxed);
    return true;
}

bool DeviceMeteringManager::getLatestLevels(DeviceId deviceId, DeviceMeterData& out) const {
    return getLatestLevels(legacyPathForDeviceId(deviceId), out);
}

void DeviceMeteringManager::setGain(const ChainNodePath& devicePath, float gain) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(devicePath);
    if (it != entries_.end()) {
        it->second->gainLinear.store(gain, std::memory_order_relaxed);
        if (it->second->realtimeTap)
            it->second->realtimeTap->gainLinear.store(gain, std::memory_order_relaxed);
    }
}

void DeviceMeteringManager::setGain(DeviceId deviceId, float gain) {
    setGain(legacyPathForDeviceId(deviceId), gain);
}

DeviceMeteringManager::GraphTap DeviceMeteringManager::acquireGraphTap(
    const ChainNodePath& devicePath) {
    juce::ScopedLock sl(lock_);
    const auto& entry = ensureEntrySharedLocked(devicePath);
    if (!entry->clientRegistered) {
        entry->measurer.addClient(entry->client);
        entry->clientRegistered = true;
    }
    return {entry, &entry->measurer, &entry->gainLinear};
}

void DeviceMeteringManager::setDirectLevels(const ChainNodePath& devicePath, float peakL,
                                            float peakR) {
    juce::ScopedLock sl(lock_);
    auto it = entries_.find(devicePath);
    if (it != entries_.end()) {
        it->second->peakL.store(peakL, std::memory_order_relaxed);
        it->second->peakR.store(peakR, std::memory_order_relaxed);
    }
}

void DeviceMeteringManager::setDirectLevels(DeviceId deviceId, float peakL, float peakR) {
    setDirectLevels(legacyPathForDeviceId(deviceId), peakL, peakR);
}

void DeviceMeteringManager::ensureEntry(const ChainNodePath& devicePath) {
    juce::ScopedLock sl(lock_);
    ensureEntryLocked(devicePath);
}

void DeviceMeteringManager::ensureEntry(DeviceId deviceId) {
    ensureEntry(legacyPathForDeviceId(deviceId));
}

DeviceMeteringManager::RealtimeTap DeviceMeteringManager::getRealtimeTap(
    const ChainNodePath& devicePath) {
    juce::ScopedLock sl(lock_);
    auto& entry = ensureEntryLocked(devicePath);

    if (!entry.realtimeTap) {
        entry.realtimeTap = std::make_shared<RealtimeTapStorage>();
        entry.realtimeTap->gainLinear.store(entry.gainLinear.load(std::memory_order_relaxed),
                                            std::memory_order_relaxed);
    }

    auto storage = entry.realtimeTap;
    return {storage, &storage->peakL, &storage->peakR, &storage->gainLinear};
}

DeviceMeteringManager::RealtimeTap DeviceMeteringManager::getRealtimeTap(DeviceId deviceId) {
    return getRealtimeTap(legacyPathForDeviceId(deviceId));
}

void DeviceMeteringManager::setRackDirectLevels(RackId rackId, float peakL, float peakR) {
    juce::ScopedLock sl(lock_);
    auto it = rackEntries_.find(rackId);
    if (it != rackEntries_.end()) {
        it->second->peakL.store(peakL, std::memory_order_relaxed);
        it->second->peakR.store(peakR, std::memory_order_relaxed);
    }
}

void DeviceMeteringManager::ensureRackEntry(RackId rackId) {
    juce::ScopedLock sl(lock_);
    if (rackEntries_.find(rackId) == rackEntries_.end())
        rackEntries_[rackId] = std::make_unique<SimpleEntry>();
}

bool DeviceMeteringManager::getRackLatestLevels(RackId rackId, DeviceMeterData& out) const {
    juce::ScopedLock sl(lock_);
    auto it = rackEntries_.find(rackId);
    if (it == rackEntries_.end())
        return false;

    out.peakL = it->second->peakL.load(std::memory_order_relaxed);
    out.peakR = it->second->peakR.load(std::memory_order_relaxed);
    return true;
}

void DeviceMeteringManager::clear() {
    juce::ScopedLock sl(lock_);
    for (auto& [devicePath, entry] : entries_) {
        if (entry->clientRegistered)
            entry->measurer.removeClient(entry->client);
    }
    // Entries a live graph tap still holds outlive this call and die with the
    // node; the audio thread keeps writing into a measurer that simply has no
    // clients left. See GraphTap for why that matters at shutdown.
    entries_.clear();
    rackEntries_.clear();
}

DeviceMeteringManager* DeviceMeteringManager::getInstanceForEdit(te::Edit& edit) {
    juce::ScopedLock lock(editMapLock_);
    const auto it = editMap_.find(&edit);
    return it != editMap_.end() ? it->second : nullptr;
}

void DeviceMeteringManager::registerForEdit(te::Edit& edit, DeviceMeteringManager* manager) {
    installDeviceMeteringTap();
    juce::ScopedLock lock(editMapLock_);
    editMap_[&edit] = manager;
}

void DeviceMeteringManager::unregisterForEdit(te::Edit& edit) {
    juce::ScopedLock lock(editMapLock_);
    editMap_.erase(&edit);
}

}  // namespace magda
