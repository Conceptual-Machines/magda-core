#include "BindingRegistry.hpp"

#include "../../audio/MidiDeviceMatch.hpp"
#include "../aliases/AliasRegistry.hpp"
#include "../aliases/ChainContext.hpp"
#include "../aliases/ResolverRegistry.hpp"
#include "../aliases/TargetResolver.hpp"

namespace magda {

BindingRegistry& BindingRegistry::getInstance() {
    static BindingRegistry instance;
    return instance;
}

// ============================================================================
// Internal helpers
// ============================================================================

static std::vector<Binding>& scopeVec(BindingScope scope, std::vector<Binding>& global,
                                      std::vector<Binding>& project) {
    return scope == BindingScope::Global ? global : project;
}

static const std::vector<Binding>& scopeVecConst(BindingScope scope,
                                                 const std::vector<Binding>& global,
                                                 const std::vector<Binding>& project) {
    return scope == BindingScope::Global ? global : project;
}

// ============================================================================
// CRUD
// ============================================================================

void BindingRegistry::add(BindingScope scope, const Binding& b) {
    auto& vec = scopeVec(scope, globalBindings_, projectBindings_);
    for (auto& existing : vec) {
        if (existing.id == b.id) {
            existing = b;
            rebuildSnapshot();
            notifyListeners(scope);
            return;
        }
    }
    vec.push_back(b);
    rebuildSnapshot();
    notifyListeners(scope);
}

void BindingRegistry::update(BindingScope scope, const Binding& b) {
    auto& vec = scopeVec(scope, globalBindings_, projectBindings_);
    for (auto& existing : vec) {
        if (existing.id == b.id) {
            existing = b;
            rebuildSnapshot();
            notifyListeners(scope);
            return;
        }
    }
}

void BindingRegistry::remove(BindingScope scope, const BindingId& id) {
    auto& vec = scopeVec(scope, globalBindings_, projectBindings_);
    auto it =
        std::remove_if(vec.begin(), vec.end(), [&id](const Binding& b) { return b.id == id; });
    if (it != vec.end()) {
        vec.erase(it, vec.end());
        rebuildSnapshot();
        notifyListeners(scope);
    }
}

// ============================================================================
// Queries
// ============================================================================

std::vector<Binding> BindingRegistry::bindings(BindingScope scope) const {
    return scopeVecConst(scope, globalBindings_, projectBindings_);
}

std::vector<Binding> BindingRegistry::findForSource(const ControllerId& controllerId,
                                                    BindingMsgType msgType, int channel,
                                                    int number) const {
    // Read from atomic snapshot -- safe on any thread
    auto snap = std::atomic_load(&snapshot_);
    std::vector<Binding> result;
    if (!snap)
        return result;

    for (const auto& b : *snap) {
        if (b.source.controllerId != controllerId)
            continue;
        if (b.source.msgType != msgType)
            continue;
        if (b.source.number != number)
            continue;
        // channel == 0 on binding means "any channel"
        if (b.source.channel != 0 && b.source.channel != channel)
            continue;
        result.push_back(b);
    }
    return result;
}

std::vector<Binding> BindingRegistry::findForPort(const juce::String& liveIdentifier,
                                                  const juce::String& liveName,
                                                  BindingMsgType msgType, int channel,
                                                  int number) const {
    auto snap = std::atomic_load(&snapshot_);
    std::vector<Binding> result;
    if (!snap)
        return result;

    for (const auto& b : *snap) {
        if (b.source.portKey.isEmpty())
            continue;
        if (!magda::midi::matches(b.source.portKey, liveIdentifier, liveName))
            continue;
        if (b.source.msgType != msgType)
            continue;
        if (b.source.number != number)
            continue;
        if (b.source.channel != 0 && b.source.channel != channel)
            continue;
        result.push_back(b);
    }
    return result;
}

// ============================================================================
// Target reverse queries
// ============================================================================

std::vector<Binding> BindingRegistry::findForTarget(const ChainNodePath& devicePath, int paramIndex,
                                                    StaticTarget::Owner owner) const {
    std::vector<Binding> results;

    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    auto checkScope = [&](const std::vector<Binding>& vec) {
        for (const auto& b : vec) {
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath && resolved.paramIndex == paramIndex &&
                resolved.owner == owner)
                results.push_back(b);
        }
    };

    checkScope(globalBindings_);
    checkScope(projectBindings_);

    return results;
}

int BindingRegistry::removeForTarget(const ChainNodePath& devicePath, int paramIndex,
                                     StaticTarget::Owner owner) {
    auto toRemove = findForTarget(devicePath, paramIndex, owner);

    for (const auto& b : toRemove) {
        // Determine scope by checking which vector contains this binding
        bool inGlobal = false;
        for (const auto& gb : globalBindings_) {
            if (gb.id == b.id) {
                inGlobal = true;
                break;
            }
        }
        remove(inGlobal ? BindingScope::Global : BindingScope::Project, b.id);
    }

    return static_cast<int>(toRemove.size());
}

// ============================================================================
// Persistence
// ============================================================================

std::vector<Binding> BindingRegistry::decodeArray(const juce::var& json) {
    std::vector<Binding> result;
    if (!json.isArray())
        return result;
    for (const auto& item : *json.getArray()) {
        if (auto b = decodeBinding(item))
            result.push_back(*b);
    }
    return result;
}

juce::var BindingRegistry::encodeArray(const std::vector<Binding>& bindings) {
    juce::Array<juce::var> arr;
    for (const auto& b : bindings)
        arr.add(encodeBinding(b));
    return juce::var(arr);
}

void BindingRegistry::loadGlobal(const juce::var& json) {
    globalBindings_ = decodeArray(json);
    rebuildSnapshot();
    notifyListeners(BindingScope::Global);
}

juce::var BindingRegistry::saveGlobal() const {
    return encodeArray(globalBindings_);
}

void BindingRegistry::loadProject(const juce::var& json) {
    projectBindings_ = decodeArray(json);
    rebuildSnapshot();
    notifyListeners(BindingScope::Project);
}

juce::var BindingRegistry::saveProject() const {
    return encodeArray(projectBindings_);
}

void BindingRegistry::clearProject() {
    projectBindings_.clear();
    rebuildSnapshot();
    notifyListeners(BindingScope::Project);
}

// ============================================================================
// Listeners
// ============================================================================

void BindingRegistry::addListener(BindingRegistryListener* l) {
    listeners_.push_back(l);
}

void BindingRegistry::removeListener(BindingRegistryListener* l) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end());
}

// ============================================================================
// Private
// ============================================================================

void BindingRegistry::rebuildSnapshot() {
    // Combine both scopes into one snapshot vector for the MIDI thread
    auto combined = std::make_shared<std::vector<Binding>>();
    combined->insert(combined->end(), globalBindings_.begin(), globalBindings_.end());
    combined->insert(combined->end(), projectBindings_.begin(), projectBindings_.end());
    std::shared_ptr<const std::vector<Binding>> snap = combined;
    std::atomic_store(&snapshot_, snap);
}

void BindingRegistry::notifyListeners(BindingScope scope) {
    auto copy = listeners_;
    for (auto* l : copy)
        if (l)
            l->bindingRegistryChanged(scope);
}

}  // namespace magda
