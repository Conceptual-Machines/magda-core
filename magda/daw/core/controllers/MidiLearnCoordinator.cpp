#include "MidiLearnCoordinator.hpp"

#include "../../audio/ControllerRouter.hpp"
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

void MidiLearnCoordinator::beginLearn(const ChainNodePath& path, int paramIndex,
                                      const juce::String& displayName) {
    armSession(path, paramIndex, StaticTarget::Owner::PluginParam, INVALID_MOD_ID, -1, displayName);
}

void MidiLearnCoordinator::beginLearnMacro(const ChainNodePath& path, int macroIndex,
                                           const juce::String& displayName) {
    armSession(path, macroIndex, StaticTarget::Owner::DeviceMacro, INVALID_MOD_ID, -1, displayName);
}

void MidiLearnCoordinator::beginLearnModParam(const ChainNodePath& path, ModId modId,
                                              int modParamIndex, const juce::String& displayName) {
    // paramIndex is unused for ModParam; pass -1 so any (path, paramIndex)-based
    // listener comparison naturally fails to match this session.
    armSession(path, -1, StaticTarget::Owner::ModParam, modId, modParamIndex, displayName);
}

void MidiLearnCoordinator::armSession(const ChainNodePath& path, int paramIndex,
                                      StaticTarget::Owner owner, ModId modId, int modParamIndex,
                                      const juce::String& displayName) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    // Cancel any prior session first
    if (armed_) {
        ChainNodePath prevPath = armedPath_;
        int prevParam = armedParam_;
        armed_ = false;
        armedPath_ = {};
        armedParam_ = -1;
        armedOwner_ = StaticTarget::Owner::PluginParam;
        armedModId_ = INVALID_MOD_ID;
        armedModParamIndex_ = -1;
        armedDisplayName_ = {};
        if (router_)
            router_->cancelLearnSession();
        notifyStateChanged(prevPath, prevParam, false);
    }

    // Arm the new session
    armed_ = true;
    armedPath_ = path;
    armedParam_ = paramIndex;
    armedOwner_ = owner;
    armedModId_ = modId;
    armedModParamIndex_ = modParamIndex;
    armedDisplayName_ = displayName;

    notifyStateChanged(path, paramIndex, true);

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
    int param = armedParam_;
    armed_ = false;
    armedPath_ = {};
    armedParam_ = -1;
    armedOwner_ = StaticTarget::Owner::PluginParam;
    armedModId_ = INVALID_MOD_ID;
    armedModParamIndex_ = -1;
    armedDisplayName_ = {};

    if (router_)
        router_->cancelLearnSession();

    notifyStateChanged(path, param, false);
    DBG("MidiLearnCoordinator: cancelled");
}

bool MidiLearnCoordinator::isLearning(const ChainNodePath& path, int paramIndex) const {
    return armed_ && armedOwner_ == StaticTarget::Owner::PluginParam && armedPath_ == path &&
           armedParam_ == paramIndex;
}

bool MidiLearnCoordinator::isLearningMacro(const ChainNodePath& path, int macroIndex) const {
    return armed_ && armedOwner_ == StaticTarget::Owner::DeviceMacro && armedPath_ == path &&
           armedParam_ == macroIndex;
}

bool MidiLearnCoordinator::isLearningModParam(const ChainNodePath& path, ModId modId,
                                              int modParamIndex) const {
    return armed_ && armedOwner_ == StaticTarget::Owner::ModParam && armedPath_ == path &&
           armedModId_ == modId && armedModParamIndex_ == modParamIndex;
}

int MidiLearnCoordinator::clearMappings(const ChainNodePath& path, int paramIndex) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    int removed = BindingRegistry::getInstance().removeForTarget(path, paramIndex);
    if (removed > 0) {
        auto copyListeners = listeners_;
        for (auto* l : copyListeners)
            if (l)
                l->midiLearnCleared(path, paramIndex, removed);
    }
    return removed;
}

int MidiLearnCoordinator::clearMacroMappings(const ChainNodePath& path, int macroIndex) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    int removed = BindingRegistry::getInstance().removeForTarget(path, macroIndex,
                                                                 StaticTarget::Owner::DeviceMacro);
    if (removed > 0) {
        auto copyListeners = listeners_;
        for (auto* l : copyListeners)
            if (l)
                l->midiLearnCleared(path, macroIndex, removed);
    }
    return removed;
}

int MidiLearnCoordinator::clearModParamMappings(const ChainNodePath& path, ModId modId,
                                                int modParamIndex) {
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    int removed = BindingRegistry::getInstance().removeForModParam(path, modId, modParamIndex);
    if (removed > 0) {
        auto copyListeners = listeners_;
        for (auto* l : copyListeners)
            if (l)
                // Existing listener API is keyed by paramIndex; pass modParamIndex so
                // anyone observing a specific mod-param can match by (path, modParamIndex).
                l->midiLearnCleared(path, modParamIndex, removed);
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
    StaticTarget::Owner owner = armedOwner_;
    ModId modId = armedModId_;
    int modParamIndex = armedModParamIndex_;

    // Reset armed state before notifying
    armed_ = false;
    armedPath_ = {};
    armedParam_ = -1;
    armedOwner_ = StaticTarget::Owner::PluginParam;
    armedModId_ = INVALID_MOD_ID;
    armedModParamIndex_ = -1;
    armedDisplayName_ = {};

    // ---- Build target ----
    Target target;
    if (owner == StaticTarget::Owner::PluginParam) {
        // Prefer alias if one exists in the registry for this (path, paramIndex).
        // Aliases are only meaningful for plugin parameters; macros and mod-params
        // always use the StaticTarget form so the captured binding survives focus
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
            StaticTarget st;
            st.devicePath = path;
            st.paramIndex = paramIndex;
            target = Target{st};
            DBG("MidiLearnCoordinator: using static target (plugin_param)");
        }
    } else {
        StaticTarget st;
        st.devicePath = path;
        st.owner = owner;
        if (owner == StaticTarget::Owner::ModParam) {
            st.modId = modId;
            st.modParamIndex = modParamIndex;
        } else {
            st.paramIndex = paramIndex;
        }
        target = Target{st};
        DBG("MidiLearnCoordinator: using static target (owner="
            << (owner == StaticTarget::Owner::DeviceMacro ? "macro" : "mod_param") << ")");
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
    auto copyListeners = listeners_;
    for (auto* l : copyListeners)
        if (l)
            l->midiLearnCompleted(path, paramIndex, binding);

    notifyStateChanged(path, paramIndex, false);
}

// ============================================================================
// Private helpers
// ============================================================================

void MidiLearnCoordinator::notifyStateChanged(const ChainNodePath& path, int paramIndex,
                                              bool learning) {
    auto copyListeners = listeners_;
    for (auto* l : copyListeners)
        if (l)
            l->midiLearnStateChanged(path, paramIndex, learning);
}

}  // namespace magda
