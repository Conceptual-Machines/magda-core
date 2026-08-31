#include "plugins/engine/PluginAssignments.hpp"

#include <memory>
#include <utility>

namespace magda::daw::audio::engine_adapter {

ActiveAssignment PluginAssignments::assign(magda::engine::DeviceKey key) {
    // Assigned rather than merged: a key that already had a handle is being
    // reused, and the previous assignment's requests must expire.
    auto handle = std::make_shared<const AssignmentHandle>();
    live_[key] = handle;
    return {.key = key, .handle = std::move(handle)};
}

ActiveAssignment PluginAssignments::current(magda::engine::DeviceKey key) const {
    const auto it = live_.find(key);
    if (it == live_.end())
        return {};
    return {.key = key, .handle = it->second};
}

LoadRequest PluginAssignments::request(magda::engine::DeviceKey key) const {
    const auto it = live_.find(key);
    if (it == live_.end())
        return {.key = key, .handle = {}};
    return {.key = key, .handle = it->second};
}

void PluginAssignments::release(magda::engine::DeviceKey key) {
    live_.erase(key);
}

void PluginAssignments::releaseAll() {
    live_.clear();
}

bool PluginAssignments::accepts(const LoadRequest& request) const {
    const auto held = request.handle.lock();
    if (held == nullptr)
        return false;

    // Locking alone is not enough. A caller holding the ActiveAssignment it was
    // handed keeps the handle alive past the release, so the live set is what
    // decides: this key must still be this assignment.
    const auto it = live_.find(request.key);
    return it != live_.end() && it->second == held;
}

}  // namespace magda::daw::audio::engine_adapter
