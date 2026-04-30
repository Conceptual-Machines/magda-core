#include "focused_api_live.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>

#include "../audio/AudioBridge.hpp"
#include "../core/MacroInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../core/aliases/ChainContext.hpp"

namespace magda {

namespace {

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

void FocusedApiLive::autoMapToFirstParams() {
    auto path = focused();
    if (!path.isValid() || bridge_ == nullptr)
        return;

    // Only meaningful for plugin-bearing nodes (devices). Racks have macros
    // but no parameters of their own — their macros always need to be
    // user-routed to the wrapped devices' params.
    if (path.getType() != ChainNodeType::Device && path.getType() != ChainNodeType::TopLevelDevice)
        return;

    auto deviceId = path.getDeviceId();
    if (deviceId == INVALID_DEVICE_ID)
        return;

    auto plugin = bridge_->getPlugin(deviceId);
    if (plugin == nullptr)
        return;

    auto params = plugin->getAutomatableParameters();
    if (params.isEmpty())
        return;

    auto& tm = TrackManager::getInstance();
    auto resolved = tm.resolvePath(path);
    const MacroArray* macros =
        (resolved.valid && resolved.device) ? &resolved.device->macros : nullptr;

    const int kNumKnobs = 8;
    const int n = std::min(kNumKnobs, params.size());
    for (int i = 0; i < n; ++i) {
        // Skip macros that are already linked. Preserves user customisation
        // and lets the script call this on every focus change without
        // trampling per-device tweaks.
        if (macros != nullptr && i < static_cast<int>(macros->size()) &&
            (*macros)[static_cast<size_t>(i)].isLinked())
            continue;

        auto* p = params[i];
        if (p == nullptr)
            continue;

        MacroTarget t;
        t.kind = MacroTarget::Kind::DeviceParam;
        t.deviceId = deviceId;
        t.paramIndex = i;

        tm.setMacroTarget(path, i, t);
        tm.setMacroLinkAmount(path, i, t, 1.0f);
        tm.setMacroName(path, i, p->getParameterName());
    }
}

}  // namespace magda
