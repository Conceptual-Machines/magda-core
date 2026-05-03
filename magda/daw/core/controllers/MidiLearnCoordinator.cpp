#include "MidiLearnCoordinator.hpp"

#include "../../audio/controllers/ControllerRouter.hpp"
#include "../aliases/AliasRegistry.hpp"
#include "../aliases/AliasReverseIndex.hpp"

namespace magda {

// ============================================================================
// Singleton
// ============================================================================

MidiLearnCoordinator& MidiLearnCoordinator::getInstance() {
    static MidiLearnCoordinator instance;
    return instance;
}

// ============================================================================
// Setup
// ============================================================================

void MidiLearnCoordinator::attach(ControllerRouter& router) {
    router_ = &router;
}

// ============================================================================
// Listener management
// ============================================================================

void MidiLearnCoordinator::addListener(MidiLearnCoordinatorListener* l) {
    if (l != nullptr && std::find(listeners_.begin(), listeners_.end(), l) == listeners_.end())
        listeners_.push_back(l);
}

void MidiLearnCoordinator::removeListener(MidiLearnCoordinatorListener* l) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end());
}

// ============================================================================
// Learn control
// ============================================================================

void MidiLearnCoordinator::beginLearn(const ControlTarget& target,
                                      const juce::String& displayName) {
    // ModParam carries identity in (modId, modParamIndex); paramIndex stays -1
    // so listener comparisons keyed on (path, paramIndex) don't false-match.
    if (target.kind == ControlTarget::Kind::ModParam) {
        armSession(target.devicePath, /*paramIndex=*/-1, target.kind, target.modId,
                   target.modParamIndex, displayName);
        return;
    }
    armSession(target.devicePath, target.paramIndex, target.kind, INVALID_MOD_ID, -1, displayName);
}

void MidiLearnCoordinator::armSession(const ChainNodePath& path, int paramIndex,
                                      ControlTarget::Kind owner, ModId modId, int modParamIndex,
                                      const juce::String& displayName) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    // Listener-facing paramIndex carries modParamIndex for ModParam so a UI
    // component can match its learn pulse by (path, paramIndex, owner) — the
    // armed_*_ fields keep -1 internally to avoid colliding with PluginParam /
    // DeviceMacro isLearning() lookups on the same (path, paramIndex) tuple.
    auto listenerParam = [](ControlTarget::Kind o, int regular, int modParam) {
        return o == ControlTarget::Kind::ModParam ? modParam : regular;
    };

    // Cancel any prior session first
    if (armed_) {
        ChainNodePath prevPath = armedPath_;
        int prevParam = listenerParam(armedOwner_, armedParam_, armedModParamIndex_);
        ControlTarget::Kind prevOwner = armedOwner_;
        armed_ = false;
        armedPath_ = {};
        armedParam_ = -1;
        armedOwner_ = ControlTarget::Kind::PluginParam;
        armedModId_ = INVALID_MOD_ID;
        armedModParamIndex_ = -1;
        armedDisplayName_ = {};
        if (router_)
            router_->cancelLearnSession();
        notifyStateChanged(prevPath, prevParam, prevOwner, false);
    }

    // Arm the new session
    armed_ = true;
    armedPath_ = path;
    armedParam_ = paramIndex;
    armedOwner_ = owner;
    armedModId_ = modId;
    armedModParamIndex_ = modParamIndex;
    armedDisplayName_ = displayName;

    notifyStateChanged(path, listenerParam(owner, paramIndex, modParamIndex), owner, true);

    if (router_) {
        LearnSessionConfig cfg;
        router_->beginLearnSession(cfg, [this](const LearnCapture& c) { onCapture(c); });
    }

    DBG("MidiLearnCoordinator: armed for '" << displayName << "'");
}

void MidiLearnCoordinator::cancelLearn() {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    if (!armed_)
        return;

    ChainNodePath path = armedPath_;
    ControlTarget::Kind owner = armedOwner_;
    int param = owner == ControlTarget::Kind::ModParam ? armedModParamIndex_ : armedParam_;
    armed_ = false;
    armedPath_ = {};
    armedParam_ = -1;
    armedOwner_ = ControlTarget::Kind::PluginParam;
    armedModId_ = INVALID_MOD_ID;
    armedModParamIndex_ = -1;
    armedDisplayName_ = {};

    if (router_)
        router_->cancelLearnSession();

    notifyStateChanged(path, param, owner, false);
    DBG("MidiLearnCoordinator: cancelled");
}

bool MidiLearnCoordinator::isLearning(const ControlTarget& target) const {
    if (!armed_ || armedOwner_ != target.kind || armedPath_ != target.devicePath)
        return false;
    if (target.kind == ControlTarget::Kind::ModParam)
        return armedModId_ == target.modId && armedModParamIndex_ == target.modParamIndex;
    return armedParam_ == target.paramIndex;
}

int MidiLearnCoordinator::clearMappings(const ControlTarget& target) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    auto& reg = BindingRegistry::getInstance();
    int removed = 0;
    int notifyParam = target.paramIndex;
    if (target.kind == ControlTarget::Kind::DeviceMacro) {
        // Leave focused-device-macro resolver (automap profile) bindings intact
        // so the macro falls back to its profile mapping after the override is cleared.
        removed = reg.removeStaticBindingsForMacro(target.devicePath, target.paramIndex);
    } else if (target.kind == ControlTarget::Kind::ModParam) {
        removed = reg.removeFor(target);
        // Listener API is keyed by paramIndex; carry modParamIndex so observers
        // keyed on (path, modParamIndex) can match.
        notifyParam = target.modParamIndex;
    } else {
        removed = reg.removeFor(target);
    }
    if (removed > 0) {
        auto copyListeners = listeners_;
        for (auto* l : copyListeners)
            if (l)
                l->midiLearnCleared(target.devicePath, notifyParam, target.kind, removed);
    }
    return removed;
}

// ============================================================================
// onCapture (message thread)
// ============================================================================

void MidiLearnCoordinator::onCapture(const LearnCapture& capture) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    if (!armed_) {
        DBG("MidiLearnCoordinator: capture arrived but session was already cancelled");
        return;
    }

    ChainNodePath path = armedPath_;
    int paramIndex = armedParam_;
    ControlTarget::Kind owner = armedOwner_;
    ModId modId = armedModId_;
    int modParamIndex = armedModParamIndex_;

    // Reset armed state before notifying
    armed_ = false;
    armedPath_ = {};
    armedParam_ = -1;
    armedOwner_ = ControlTarget::Kind::PluginParam;
    armedModId_ = INVALID_MOD_ID;
    armedModParamIndex_ = -1;
    armedDisplayName_ = {};

    // ---- Build target ----
    Target target;
    if (owner == ControlTarget::Kind::PluginParam) {
        // Prefer alias if one exists in the registry for this (path, paramIndex).
        // Aliases are only meaningful for plugin parameters; macros and mod-params
        // always use the ControlTarget form so the captured binding survives focus
        // changes that would invalidate alias resolution.
        auto bestAlias = bestAliasForPath(AliasRegistry::getInstance(), path, paramIndex, true);
        if (bestAlias.has_value()) {
            // Extract pluginType from the canonical name: format is "pluginType.paramName"
            // e.g. "serum.filter_cutoff" -> pluginType = "serum"
            juce::String canonicalName = *bestAlias;
            juce::String pluginType;
            int dotPos = canonicalName.indexOfChar('.');
            if (dotPos > 0)
                pluginType = canonicalName.substring(0, dotPos);

            AliasRef aliasRef;
            aliasRef.name = canonicalName;
            aliasRef.pluginType = pluginType;
            target = Target{aliasRef};
            DBG("MidiLearnCoordinator: using alias target '" << canonicalName << "'");
        } else {
            ControlTarget st;
            st.devicePath = path;
            st.paramIndex = paramIndex;
            target = Target{st};
            DBG("MidiLearnCoordinator: using static target (plugin_param)");
        }
    } else {
        ControlTarget st;
        st.devicePath = path;
        st.kind = owner;
        if (owner == ControlTarget::Kind::ModParam) {
            st.modId = modId;
            st.modParamIndex = modParamIndex;
        } else {
            st.paramIndex = paramIndex;
        }
        target = Target{st};
        DBG("MidiLearnCoordinator: using static target (owner="
            << (owner == ControlTarget::Kind::DeviceMacro ? "macro" : "mod_param") << ")");
    }

    // ---- Build source ----
    // Prefer port-based addressing (display name is stable across machines).
    // controllerId stays set only when Learn arrived via a registered scripted
    // surface; ordinary user gestures don't go through ControllerRegistry.
    BindingSource source;
    source.portKey = capture.portName.isNotEmpty() ? capture.portName : capture.portId;
    source.controllerId = capture.controllerId;
    source.msgType = capture.msgType;
    source.channel = 0;  // any channel (lockChannel is false in default config)
    source.number = capture.number;

    // ---- Build binding ----
    Binding binding;
    binding.id = juce::Uuid();
    binding.source = source;
    binding.target = target;
    binding.mode = BindingMode::Absolute;
    binding.range = BindingRange{0.0f, 1.0f, BindingCurve::Linear};

    // Learn always stores at Project scope. Global-scope bindings are reserved
    // for future controller templates/scripts that are driven by the hardware
    // layer, not by user-level "map this knob" gestures.
    BindingRegistry::getInstance().add(BindingScope::Project, binding);

    DBG("MidiLearnCoordinator: project binding created");

    // ---- Notify listeners ----
    // For ModParam, the listener-facing paramIndex carries modParamIndex so listeners
    // keyed on (path, paramIndex, owner) can disambiguate modifier targets within a scope.
    const int notifyParam = owner == ControlTarget::Kind::ModParam ? modParamIndex : paramIndex;
    auto copyListeners = listeners_;
    for (auto* l : copyListeners)
        if (l)
            l->midiLearnCompleted(path, notifyParam, owner, binding);

    notifyStateChanged(path, notifyParam, owner, false);
}

// ============================================================================
// Private helpers
// ============================================================================

void MidiLearnCoordinator::notifyStateChanged(const ChainNodePath& path, int paramIndex,
                                              ControlTarget::Kind owner, bool learning) {
    auto copyListeners = listeners_;
    for (auto* l : copyListeners)
        if (l)
            l->midiLearnStateChanged(path, paramIndex, owner, learning);
}

}  // namespace magda
