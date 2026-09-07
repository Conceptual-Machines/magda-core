#include "ControllerRegistry.hpp"

#include "../../audio/midi/MidiDeviceMatch.hpp"

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
    const auto matchesId = [&c](const Controller& existing) { return existing.id == c.id; };
    if (const auto found = std::ranges::find_if(controllers_, matchesId);
        found != controllers_.end()) {
        *found = c;
    } else {
        controllers_.push_back(c);
    }
    rebuildSnapshot();
    notifyListeners();
}

void ControllerRegistry::update(const Controller& c) {
    const auto matchesId = [&c](const Controller& existing) { return existing.id == c.id; };
    if (const auto found = std::ranges::find_if(controllers_, matchesId);
        found != controllers_.end()) {
        *found = c;
        rebuildSnapshot();
        notifyListeners();
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

bool ControllerRegistry::rematchInputPorts(const juce::Array<juce::MidiDeviceInfo>& liveInputs) {
    bool changed = false;
    for (auto& c : controllers_) {
        const auto matchesIdentifier = [&c](const juce::MidiDeviceInfo& dev) {
            return dev.identifier == c.inputPort;
        };
        const bool identifierLive = std::ranges::any_of(liveInputs, matchesIdentifier);
        if (identifierLive || c.inputPortName.isEmpty())
            continue;

        const auto matchesName = [&c](const juce::MidiDeviceInfo& dev) {
            return dev.name == c.inputPortName;
        };
        if (const auto* const found = std::ranges::find_if(liveInputs, matchesName);
            found != liveInputs.end()) {
            c.inputPort = found->identifier;
            changed = true;
        }
    }
    if (changed) {
        rebuildSnapshot();
        notifyListeners();
    }
    return changed;
}

// ============================================================================
// Queries
// ============================================================================

std::vector<Controller> ControllerRegistry::all() const {
    return controllers_;
}

std::optional<Controller> ControllerRegistry::find(const ControllerId& id) const {
    const auto matchesId = [&id](const Controller& c) { return c.id == id; };
    const auto found = std::ranges::find_if(controllers_, matchesId);
    return found == controllers_.end() ? std::nullopt : std::make_optional(*found);
}

std::optional<Controller> ControllerRegistry::findByInputPort(const juce::String& portId) const {
    const auto matchesPort = [&portId](const Controller& c) { return c.inputPort == portId; };
    const auto found = std::ranges::find_if(controllers_, matchesPort);
    return found == controllers_.end() ? std::nullopt : std::make_optional(*found);
}

bool ControllerRegistry::isControllerInputPort(const juce::String& portId) const {
    // Reads from atomic snapshot -- safe on any thread
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return false;
    const auto matchesPort = [&portId](const Controller& c) { return c.inputPort == portId; };
    return std::ranges::any_of(*snap, matchesPort);
}

bool ControllerRegistry::isControllerInputPort(const juce::String& liveIdentifier,
                                               const juce::String& liveName) const {
    // Reads from atomic snapshot -- safe on any thread
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return false;
    const auto matchesLive = [&](const Controller& c) {
        return magda::midi::matches(c.inputPort, liveIdentifier, liveName);
    };
    return std::ranges::any_of(*snap, matchesLive);
}

// ============================================================================
// Persistence
// ============================================================================

void ControllerRegistry::loadFromConfig(const juce::var& json) {
    controllers_.clear();

    DBG("ControllerRegistry::loadFromConfig - input isArray="
        << (json.isArray() ? "yes" : "no") << " isVoid=" << (json.isVoid() ? "yes" : "no")
        << " dump=" << juce::JSON::toString(json, true));

    if (json.isArray()) {
        int index = 0;
        for (const auto& item : *json.getArray()) {
            auto c = decodeController(item);
            if (c) {
                controllers_.push_back(*c);
                DBG("ControllerRegistry:   [" << index << "] loaded name='" << c->name
                                              << "' inputPort='" << c->inputPort << "'");
            } else {
                DBG("ControllerRegistry:   ["
                    << index << "] FAILED to decode entry: " << juce::JSON::toString(item, true));
            }
            ++index;
        }
    }

    DBG("ControllerRegistry::loadFromConfig - " << (int)controllers_.size()
                                                << " controller(s) loaded");

    rebuildSnapshot();
    notifyListeners();
}

juce::var ControllerRegistry::saveToConfig() const {
    juce::Array<juce::var> arr;
    for (const auto& c : controllers_)
        arr.add(encodeController(c));
    return {arr};
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
