#include "focused_api_live.hpp"

#include "../core/MacroInfo.hpp"
#include "../core/RackInfo.hpp"
#include "../core/SelectionManager.hpp"
#include "../core/TrackManager.hpp"
#include "../core/aliases/ChainContext.hpp"
#include "../core/aliases/Target.hpp"
#include "../core/controllers/Binding.hpp"
#include "../core/controllers/BindingRegistry.hpp"

namespace magda {

namespace {

// Sentinel ControllerId for Lua-script-driven automap bindings. Stable across
// engages so engageAutoMap can wipe stale bindings from a prior load before
// reinstalling fresh ones. Not registered in ControllerRegistry, so the
// router never matches these bindings — they exist purely so the BindingRegistry
// dot-detection (hasResolverBindingForDevice / hasActiveBindingFor)
// lights the green automap dot on the focused device's header and macros.
const juce::Uuid& luaAutomapSentinel() {
    static const juce::Uuid id{"00000000-0000-0000-0000-000000000001"};
    return id;
}

constexpr int kAutomapMacroCount = 8;

// Locate the macro array for the focused-macro-owner path. Returns nullptr
// when no focus, when the path doesn't resolve, or when the resolved node
// type doesn't carry macros.
const MacroArray* macrosForFocusedPath(const ChainNodePath& path) {
    if (!path.isValid())
        return nullptr;

    auto& tm = TrackManager::getInstance();

    // Track-level focus: macros live on the TrackInfo itself.
    if (path.getType() == ChainNodeType::Track) {
        const auto* track = tm.getTrack(path.trackId);
        return track ? &track->macros : nullptr;
    }

    auto resolved = tm.resolvePath(path);
    if (!resolved.valid)
        return nullptr;

    if (resolved.rack)
        return &resolved.rack->macros;
    if (resolved.device)
        return &resolved.device->macros;
    return nullptr;
}

ChainNodePath focused() {
    DefaultChainContext ctx;
    return ctx.focusedMacroOwner();
}

}  // namespace

bool FocusedApiLive::hasFocus() const {
    return focused().isValid();
}

juce::String FocusedApiLive::getFocusedName() const {
    auto path = focused();
    if (!path.isValid())
        return {};

    auto& tm = TrackManager::getInstance();

    if (path.getType() == ChainNodeType::Track) {
        const auto* track = tm.getTrack(path.trackId);
        return track ? track->name : juce::String{};
    }

    auto resolved = tm.resolvePath(path);
    if (!resolved.valid)
        return {};
    if (resolved.rack)
        return resolved.rack->name;
    if (resolved.device)
        return resolved.device->name;
    return {};
}

juce::String FocusedApiLive::getMacroName(int idx) const {
    const auto* macros = macrosForFocusedPath(focused());
    if (macros == nullptr || idx < 0 || idx >= static_cast<int>(macros->size()))
        return {};
    return (*macros)[static_cast<size_t>(idx)].name;
}

float FocusedApiLive::getMacroValue(int idx) const {
    const auto* macros = macrosForFocusedPath(focused());
    if (macros == nullptr || idx < 0 || idx >= static_cast<int>(macros->size()))
        return 0.0f;
    return (*macros)[static_cast<size_t>(idx)].value;
}

void FocusedApiLive::setMacroValue(int idx, float value) {
    auto path = focused();
    if (!path.isValid())
        return;
    // Reuse TrackManager's setter - same path used by ControllerParamWriter
    // for static `focused.macro` bindings, so script writes and JSON-profile
    // writes converge on identical state mutation + listener notification.
    TrackManager::getInstance().setMacroValue(path, idx, value);
}

void FocusedApiLive::engageAutoMap() {
    auto& reg = BindingRegistry::getInstance();
    const auto& sentinel = luaAutomapSentinel();

    // Wipe any leftover bindings from a prior engage (or a prior session
    // that didn't tear down cleanly) before adding fresh ones, so re-engaging
    // is idempotent.
    reg.removeAllForController(BindingScope::Global, sentinel);

    for (int i = 0; i < kAutomapMacroCount; ++i) {
        juce::StringPairArray args;
        args.set("macroIndex", juce::String{i});

        Binding b;
        b.id = juce::Uuid();
        b.source.controllerId = sentinel;
        b.source.msgType = BindingMsgType::CC;
        b.source.channel = 0;  // any
        b.source.number = 0;
        b.target = ResolverRef{"focused.macro", args};
        b.mode = BindingMode::Absolute;
        reg.add(BindingScope::Global, b);
    }
}

void FocusedApiLive::clearAutoMap() {
    BindingRegistry::getInstance().removeAllForController(BindingScope::Global,
                                                          luaAutomapSentinel());
}

void FocusedApiLive::cycleDevice(int direction) {
    if (direction == 0)
        return;
    direction = direction > 0 ? 1 : -1;

    auto& sel = SelectionManager::getInstance();
    const auto selectedTrack = sel.getSelectedTrack();
    if (selectedTrack == INVALID_TRACK_ID)
        return;

    const auto& elements = TrackManager::getInstance().getChainElements(selectedTrack);
    if (elements.empty())
        return;

    std::vector<ChainNodePath> chainPaths;
    chainPaths.reserve(elements.size());
    for (const auto& elem : elements) {
        if (isRack(elem)) {
            chainPaths.push_back(ChainNodePath::rack(selectedTrack, getRack(elem).id));
        } else {
            chainPaths.push_back(ChainNodePath::topLevelDevice(selectedTrack, getDevice(elem).id));
        }
    }
    if (chainPaths.empty())
        return;

    int currentIdx = -1;
    if (sel.hasChainNodeSelection()) {
        const auto current = sel.getSelectedChainNode();
        for (int i = 0; i < static_cast<int>(chainPaths.size()); ++i) {
            if (chainPaths[static_cast<size_t>(i)] == current) {
                currentIdx = i;
                break;
            }
        }
    }

    const int n = static_cast<int>(chainPaths.size());
    const int nextIdx =
        currentIdx < 0 ? (direction > 0 ? 0 : n - 1) : ((currentIdx + direction) % n + n) % n;
    sel.selectChainNode(chainPaths[static_cast<size_t>(nextIdx)]);
}

}  // namespace magda
