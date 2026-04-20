#include "ControllerRegistry.hpp"

namespace magda {

ControllerRegistry& ControllerRegistry::getInstance() {
    static ControllerRegistry instance;
    return instance;
}

// ============================================================================
// CRUD
// ============================================================================

void ControllerRegistry::add(const Controller& c) {
    // Update if exists, otherwise append
    for (auto& existing : controllers_) {
        if (existing.id == c.id) {
            existing = c;
            rebuildSnapshot();
            notifyListeners();
            return;
        }
    }
    controllers_.push_back(c);
    rebuildSnapshot();
    notifyListeners();
}

void ControllerRegistry::update(const Controller& c) {
    for (auto& existing : controllers_) {
        if (existing.id == c.id) {
            existing = c;
            rebuildSnapshot();
            notifyListeners();
            return;
        }
    }
}

void ControllerRegistry::remove(const ControllerId& id) {
    auto it = std::remove_if(controllers_.begin(), controllers_.end(),
                             [&id](const Controller& c) { return c.id == id; });
    if (it != controllers_.end()) {
        controllers_.erase(it, controllers_.end());
        rebuildSnapshot();
        notifyListeners();
    }
}

void ControllerRegistry::setEnabled(const ControllerId& id, bool enabled) {
    for (auto& c : controllers_) {
        if (c.id == id) {
            c.enabled = enabled;
            rebuildSnapshot();
            notifyListeners();
            return;
        }
    }
}

// ============================================================================
// Queries
// ============================================================================

std::vector<Controller> ControllerRegistry::all() const {
    return controllers_;
}

std::optional<Controller> ControllerRegistry::find(const ControllerId& id) const {
    for (const auto& c : controllers_)
        if (c.id == id)
            return c;
    return std::nullopt;
}

std::optional<Controller> ControllerRegistry::findByInputPort(const juce::String& portId) const {
    for (const auto& c : controllers_)
        if (c.inputPort == portId)
            return c;
    return std::nullopt;
}

bool ControllerRegistry::isControllerInputPort(const juce::String& portId) const {
    // Reads from atomic snapshot -- safe on any thread
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return false;
    for (const auto& c : *snap)
        if (c.enabled && c.inputPort == portId)
            return true;
    return false;
}

// ============================================================================
// Persistence
// ============================================================================

void ControllerRegistry::loadFromConfig(const juce::var& json) {
    controllers_.clear();

    if (json.isArray()) {
        for (const auto& item : *json.getArray()) {
            if (auto c = decodeController(item))
                controllers_.push_back(*c);
        }
    }

    rebuildSnapshot();
    notifyListeners();
}

juce::var ControllerRegistry::saveToConfig() const {
    juce::Array<juce::var> arr;
    for (const auto& c : controllers_)
        arr.add(encodeController(c));
    return juce::var(arr);
}

// ============================================================================
// Listeners
// ============================================================================

void ControllerRegistry::addListener(ControllerRegistryListener* l) {
    listeners_.push_back(l);
}

void ControllerRegistry::removeListener(ControllerRegistryListener* l) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end());
}

// ============================================================================
// Private
// ============================================================================

void ControllerRegistry::rebuildSnapshot() {
    std::shared_ptr<const std::vector<Controller>> newSnap =
        std::make_shared<const std::vector<Controller>>(controllers_);
    std::atomic_store(&snapshot_, newSnap);
}

void ControllerRegistry::notifyListeners() {
    auto copy = listeners_;
    for (auto* l : copy)
        if (l)
            l->controllerRegistryChanged();
}

}  // namespace magda
