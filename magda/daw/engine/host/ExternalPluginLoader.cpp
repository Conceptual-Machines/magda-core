#include "ExternalPluginLoader.hpp"

#include <set>
#include <utility>

namespace magda::daw::engine_host {

namespace adapter = magda::daw::audio::engine_adapter;

namespace {

/// Which plugin a slot is asking for, as one string to compare.
///
/// Every field the scan is searched by (ExternalPluginLookup.hpp), and nothing
/// a load is entitled to correct: the role, the bus widths and the MIDI flags
/// are all written back from the live instance, and including one of them would
/// make a successful load look like the slot asking for something else.
juce::String pluginIdentityOf(const DeviceInfo& device) {
    return device.uniqueId + "|" + device.fileOrIdentifier + "|" + device.name + "|" +
           device.manufacturer + "|" + device.getFormatString();
}

}  // namespace

ExternalPluginLoader::ExternalPluginLoader(adapter::CurrentDeviceLookup currentDevice,
                                           Loaded loaded)
    : currentDevice_(std::move(currentDevice)), loaded_(std::move(loaded)) {}

void ExternalPluginLoader::setServices(juce::AudioPluginFormatManager* formats,
                                       const juce::KnownPluginList* knownPlugins) {
    services_.formats = formats;
    services_.knownPlugins = knownPlugins;
}

void ExternalPluginLoader::setContext(const engine::RenderContext& context) {
    services_.context = context;
}

void ExternalPluginLoader::syncAssignments(const std::map<engine::DeviceKey, DeviceInfo>& devices) {
    std::set<engine::DeviceKey> named;

    for (const auto& [key, device] : devices) {
        if (!adapter::isExternalDevice(device))
            continue;

        named.insert(key);
        auto identity = pluginIdentityOf(device);
        const auto found = slots_.find(key);

        if (found == slots_.end()) {
            assignments_.ensureAssignment(key);
            slots_.emplace(key, Slot{.identity = std::move(identity), .generation = ++generation_});
            continue;
        }

        if (found->second.identity == identity) {
            // The ordinary case, and the one that must not mint an assignment:
            // a plan compiled again for an unrelated edit re-registers every
            // device in the project, and a new assignment each time would
            // expire the load that is still running.
            assignments_.ensureAssignment(key);
            continue;
        }

        // Every load in flight against the old assignment expires here; the
        // bound instance is retired by the rebuild instead (#2572).
        assignments_.replaceAssignment(key);
        found->second = Slot{.identity = std::move(identity), .generation = ++generation_};
    }

    for (auto slot = slots_.begin(); slot != slots_.end();) {
        if (named.contains(slot->first)) {
            ++slot;
            continue;
        }

        assignments_.release(slot->first);
        slot = slots_.erase(slot);
    }
}

void ExternalPluginLoader::forgetSlots() {
    assignments_.releaseAll();
    slots_.clear();
}

std::unique_ptr<engine::EngineDevice> ExternalPluginLoader::device(engine::DeviceKey key,
                                                                   const DeviceInfo& model) {
    auto found = slots_.find(key);

    // Nothing registered this key, so a load started for it would be refused at
    // completion anyway (PluginAssignments.hpp). Only reachable if a plan names
    // a device the publish's own model did not.
    if (found == slots_.end())
        return nullptr;

    if (found->second.instance != nullptr)
        return std::exchange(found->second.instance, nullptr);

    if (found->second.pending || found->second.spent)
        return nullptr;

    found->second.pending = true;
    const auto resolved = adapter::createEngineExternalDeviceAsync(
        model, key, services_, /*offlineRender=*/false, assignments_, currentDevice_,
        [this, key, generation = found->second.generation](adapter::ExternalDeviceResult result) {
            complete(key, generation, std::move(result));
        });

    // Asked again, because a format that answered without deferring has already
    // run complete() by here -- which is also how a resolution failure arrives.
    found = slots_.find(key);
    if (found == slots_.end())
        return nullptr;

    // A plugin the scan has never heard of has spent nothing, and a scan
    // finishing is exactly what changes the answer. So it is asked again at the
    // next publish, which costs a walk of the known list; anything that got as
    // far as instantiating is left spent.
    if (!resolved)
        found->second.spent = false;

    return std::exchange(found->second.instance, nullptr);
}

std::size_t ExternalPluginLoader::held() const {
    std::size_t held = 0;
    for (const auto& [key, slot] : slots_)
        held += slot.instance != nullptr ? 1 : 0;

    return held;
}

std::size_t ExternalPluginLoader::loading() const {
    std::size_t loading = 0;
    for (const auto& [key, slot] : slots_)
        loading += slot.pending ? 1 : 0;

    return loading;
}

void ExternalPluginLoader::complete(engine::DeviceKey key, std::uint64_t generation,
                                    adapter::ExternalDeviceResult result) {
    const auto found = slots_.find(key);

    // The load was started against an assignment this slot no longer holds, or
    // against a slot that has gone. completeExternalPluginLoad() has already
    // refused it on the same grounds; this is the loader's own bookkeeping
    // declining to write a stale answer over the one it is waiting for.
    if (found == slots_.end() || found->second.generation != generation)
        return;

    auto& slot = found->second;
    slot.pending = false;

    if (result.device == nullptr) {
        slot.spent = true;

        if (!std::exchange(slot.reported, true))
            juce::Logger::writeToLog("[engine] " + result.failure);

        return;
    }

    slot.instance = std::move(result.device);
    juce::Logger::writeToLog("[engine] loaded \"" + result.resolvedDevice->name + "\"");

    loaded_(key, *result.resolvedDevice, result.restoredParameters);
}

}  // namespace magda::daw::engine_host
