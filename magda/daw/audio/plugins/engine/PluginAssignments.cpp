#include "plugins/engine/PluginAssignments.hpp"

#include <memory>
#include <utility>

namespace magda::daw::audio::engine_adapter {

std::shared_ptr<const AssignmentHandle> AssignmentTable::find(magda::engine::DeviceKey key) const {
    const auto it = byKey_.find(key);
    return it != byKey_.end() ? it->second : nullptr;
}

bool LoadRequest::isStillWanted() const {
    const auto live = table.lock();
    if (live == nullptr)
        return false;

    const auto held = handle.lock();
    if (held == nullptr)
        return false;

    // Locking is not enough on its own. A caller holding the ActiveAssignment
    // it was handed keeps the handle alive past the release, so the table is
    // what decides: this key must still be this assignment.
    return live->find(key) == held;
}

bool LoadRequest::keyWasReassigned() const {
    const auto live = table.lock();
    if (live == nullptr)
        return false;

    const auto now = live->find(key);
    return now != nullptr && now != handle.lock();
}

bool LoadRequest::runtimeIsAlive() const {
    return !table.expired();
}

PluginAssignments::PluginAssignments() : table_(std::make_shared<AssignmentTable>()) {}

ActiveAssignment PluginAssignments::ensureAssignment(magda::engine::DeviceKey key) {
    if (auto held = table_->find(key); held != nullptr)
        return {.key = key, .handle = std::move(held)};

    return replaceAssignment(key);
}

ActiveAssignment PluginAssignments::replaceAssignment(magda::engine::DeviceKey key) {
    // Assigned rather than merged: a key that already had a handle is being
    // reused, and the previous assignment's requests must expire.
    auto handle = std::make_shared<const AssignmentHandle>();
    table_->byKey_[key] = handle;
    return {.key = key, .handle = std::move(handle)};
}

ActiveAssignment PluginAssignments::current(magda::engine::DeviceKey key) const {
    auto held = table_->find(key);
    if (held == nullptr)
        return {};
    return {.key = key, .handle = std::move(held)};
}

LoadRequest PluginAssignments::request(magda::engine::DeviceKey key) const {
    return {.key = key, .handle = table_->find(key), .table = table_};
}

void PluginAssignments::release(magda::engine::DeviceKey key) {
    table_->byKey_.erase(key);
}

void PluginAssignments::releaseAll() {
    table_->byKey_.clear();
}

std::size_t PluginAssignments::size() const {
    return table_->byKey_.size();
}

}  // namespace magda::daw::audio::engine_adapter
