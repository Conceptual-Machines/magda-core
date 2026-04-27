#include "BindingRegistry.hpp"

#include "../../audio/MidiDeviceMatch.hpp"
#include "../aliases/AliasRegistry.hpp"
#include "../aliases/ChainContext.hpp"
#include "../aliases/ResolverRegistry.hpp"
#include "../aliases/TargetResolver.hpp"
#include "ControllerRegistry.hpp"

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

int BindingRegistry::removeAllForController(BindingScope scope, const ControllerId& controllerId) {
    auto& vec = scopeVec(scope, globalBindings_, projectBindings_);
    auto it = std::remove_if(vec.begin(), vec.end(), [&controllerId](const Binding& b) {
        return b.source.controllerId == controllerId;
    });
    if (it == vec.end())
        return 0;
    int removed = static_cast<int>(std::distance(it, vec.end()));
    vec.erase(it, vec.end());
    rebuildSnapshot();
    notifyListeners(scope);
    return removed;
}

bool BindingRegistry::hasAnyBindingForController(const ControllerId& controllerId) const {
    auto match = [&controllerId](const Binding& b) {
        return b.source.controllerId == controllerId;
    };
    return std::any_of(globalBindings_.begin(), globalBindings_.end(), match) ||
           std::any_of(projectBindings_.begin(), projectBindings_.end(), match);
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

namespace {

// True when two binding sources would receive the same incoming MIDI event:
// matching msgType + number, compatible channel (either side 0 = any), and
// either a shared controllerId or a portKey match via magda::midi::matches.
bool sourcesOverlap(const BindingSource& a, const BindingSource& b) {
    if (a.msgType != b.msgType)
        return false;
    if (a.number != b.number)
        return false;
    if (a.channel != 0 && b.channel != 0 && a.channel != b.channel)
        return false;

    bool ctrlMatch = !a.controllerId.isNull() && a.controllerId == b.controllerId;
    bool portMatch = a.portKey.isNotEmpty() && b.portKey.isNotEmpty() &&
                     magda::midi::matches(a.portKey, b.portKey, b.portKey);
    return ctrlMatch || portMatch;
}

bool isFocusedDeviceMacroResolver(const Target& t) {
    if (auto* rr = std::get_if<ResolverRef>(&t))
        return rr->kind == "focused.macro";
    return false;
}

// True for explicit user mappings to a plugin parameter — i.e. anything that
// represents a Learn or hand-edited binding rather than an automap profile
// default. StaticTarget{PluginParam} is the obvious case; AliasRef is also
// included because MidiLearnCoordinator prefers an alias target whenever a
// canonical alias exists for the param (e.g. "4osc.filter_freq"), and aliases
// always resolve to a plugin parameter in this codebase. ResolverRef bindings
// (focused.macro and any future kinds) are profile-driven defaults and
// do not count.
bool isExplicitPluginParamTarget(const Target& t) {
    if (auto* st = std::get_if<StaticTarget>(&t))
        return st->owner == StaticTarget::Owner::PluginParam;
    if (std::holds_alternative<AliasRef>(t))
        return true;
    return false;
}

}  // namespace

bool BindingRegistry::hasBindingForDevice(const ChainNodePath& devicePath,
                                          StaticTarget::Owner owner) const {
    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    auto check = [&](const std::vector<Binding>& vec) -> bool {
        for (const auto& b : vec) {
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath && resolved.owner == owner)
                return true;
        }
        return false;
    };

    return check(globalBindings_) || check(projectBindings_);
}

bool BindingRegistry::hasActiveBindingForTarget(const ChainNodePath& devicePath, int paramIndex,
                                                StaticTarget::Owner owner) const {
    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    auto check = [&](const std::vector<Binding>& vec) -> bool {
        for (const auto& b : vec) {
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath && resolved.paramIndex == paramIndex &&
                resolved.owner == owner)
                return true;
        }
        return false;
    };

    return check(globalBindings_) || check(projectBindings_);
}

bool BindingRegistry::hasResolverBindingForDevice(const ChainNodePath& devicePath) const {
    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};
    auto check = [&](const std::vector<Binding>& vec) -> bool {
        for (const auto& b : vec) {
            if (!std::holds_alternative<ResolverRef>(b.target))
                continue;
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath)
                return true;
        }
        return false;
    };
    return check(globalBindings_) || check(projectBindings_);
}

bool BindingRegistry::hasUserMappingForDevice(const ChainNodePath& devicePath) const {
    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};
    auto check = [&](const std::vector<Binding>& vec) -> bool {
        for (const auto& b : vec) {
            if (std::holds_alternative<ResolverRef>(b.target))
                continue;  // resolver bindings are profile defaults, not user mappings
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath)
                return true;
        }
        return false;
    };
    return check(globalBindings_) || check(projectBindings_);
}

bool BindingRegistry::hasActiveStaticBindingForMacro(const ChainNodePath& devicePath,
                                                     int macroIndex) const {
    auto check = [&](const std::vector<Binding>& vec) -> bool {
        for (const auto& b : vec) {
            auto* st = std::get_if<StaticTarget>(&b.target);
            if (st == nullptr)
                continue;
            if (st->owner != StaticTarget::Owner::DeviceMacro)
                continue;
            if (st->devicePath != devicePath || st->paramIndex != macroIndex)
                continue;
            return true;
        }
        return false;
    };
    return check(globalBindings_) || check(projectBindings_);
}

int BindingRegistry::removeStaticBindingsForMacro(const ChainNodePath& devicePath, int macroIndex) {
    std::vector<BindingId> toRemoveGlobal;
    std::vector<BindingId> toRemoveProject;
    auto collect = [&](const std::vector<Binding>& vec, std::vector<BindingId>& out) {
        for (const auto& b : vec) {
            auto* st = std::get_if<StaticTarget>(&b.target);
            if (st == nullptr)
                continue;
            if (st->owner != StaticTarget::Owner::DeviceMacro)
                continue;
            if (st->devicePath != devicePath || st->paramIndex != macroIndex)
                continue;
            out.push_back(b.id);
        }
    };
    collect(globalBindings_, toRemoveGlobal);
    collect(projectBindings_, toRemoveProject);
    for (const auto& id : toRemoveGlobal)
        remove(BindingScope::Global, id);
    for (const auto& id : toRemoveProject)
        remove(BindingScope::Project, id);
    return static_cast<int>(toRemoveGlobal.size() + toRemoveProject.size());
}

bool BindingRegistry::isAutomapShadowedForMacro(const ChainNodePath& devicePath,
                                                int macroIndex) const {
    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    // 1. Collect sources of focused-device-macro resolver bindings that
    //    currently resolve to (devicePath, macroIndex, DeviceMacro).
    std::vector<BindingSource> automapSources;
    auto collect = [&](const std::vector<Binding>& vec) {
        for (const auto& b : vec) {
            if (!isFocusedDeviceMacroResolver(b.target))
                continue;
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath && resolved.paramIndex == macroIndex &&
                resolved.owner == StaticTarget::Owner::DeviceMacro)
                automapSources.push_back(b.source);
        }
    };
    collect(globalBindings_);
    collect(projectBindings_);
    if (automapSources.empty())
        return false;

    // 2. Look for an active static PluginParam binding whose source overlaps.
    auto hasOverride = [&](const std::vector<Binding>& vec) {
        for (const auto& b : vec) {
            if (!isExplicitPluginParamTarget(b.target))
                continue;
            for (const auto& s : automapSources)
                if (sourcesOverlap(b.source, s))
                    return true;
        }
        return false;
    };
    return hasOverride(globalBindings_) || hasOverride(projectBindings_);
}

bool BindingRegistry::isPluginParamOverridingMacro(const ChainNodePath& devicePath,
                                                   int paramIndex) const {
    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    // 1. Collect sources of static PluginParam bindings that resolve to
    //    (devicePath, paramIndex, PluginParam).
    std::vector<BindingSource> staticSources;
    auto collect = [&](const std::vector<Binding>& vec) {
        for (const auto& b : vec) {
            if (!isExplicitPluginParamTarget(b.target))
                continue;
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.devicePath == devicePath && resolved.paramIndex == paramIndex &&
                resolved.owner == StaticTarget::Owner::PluginParam)
                staticSources.push_back(b.source);
        }
    };
    collect(globalBindings_);
    collect(projectBindings_);
    if (staticSources.empty())
        return false;

    // 2. Look for an active focused-device-macro resolver binding with an
    //    overlapping source. The resolver does not need to currently resolve —
    //    its presence with a matching CC means the override is in effect for
    //    whichever device is focused.
    auto hasShadowed = [&](const std::vector<Binding>& vec) {
        for (const auto& b : vec) {
            if (!isFocusedDeviceMacroResolver(b.target))
                continue;
            for (const auto& s : staticSources)
                if (sourcesOverlap(b.source, s))
                    return true;
        }
        return false;
    };
    return hasShadowed(globalBindings_) || hasShadowed(projectBindings_);
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

std::vector<Binding> BindingRegistry::findForModParam(const ChainNodePath& devicePath, ModId modId,
                                                      int modParamIndex) const {
    std::vector<Binding> results;

    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    auto checkScope = [&](const std::vector<Binding>& vec) {
        for (const auto& b : vec) {
            auto resolved = resolver.resolve(b.target);
            if (!resolved.ok())
                continue;
            if (resolved.owner == StaticTarget::Owner::ModParam &&
                resolved.devicePath == devicePath && resolved.modId == modId &&
                resolved.modParamIndex == modParamIndex)
                results.push_back(b);
        }
    };

    checkScope(globalBindings_);
    checkScope(projectBindings_);

    return results;
}

int BindingRegistry::removeForModParam(const ChainNodePath& devicePath, ModId modId,
                                       int modParamIndex) {
    auto toRemove = findForModParam(devicePath, modId, modParamIndex);
    for (const auto& b : toRemove) {
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

bool BindingRegistry::hasActiveBindingForModParam(const ChainNodePath& devicePath, ModId modId,
                                                  int modParamIndex) const {
    return !findForModParam(devicePath, modId, modParamIndex).empty();
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
