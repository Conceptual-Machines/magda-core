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
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());

    // Cancel any prior session first
    if (armed_) {
        ChainNodePath prevPath = armedPath_;
        int prevParam = armedParam_;
        armed_ = false;
        armedPath_ = {};
        armedParam_ = -1;
        armedDisplayName_ = {};
        if (router_)
            router_->cancelLearnSession();
        notifyStateChanged(prevPath, prevParam, false);
    }

    // Arm the new session
    armed_ = true;
    armedPath_ = path;
    armedParam_ = paramIndex;
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
    armedDisplayName_ = {};

    if (router_)
        router_->cancelLearnSession();

    notifyStateChanged(path, param, false);
    DBG("MidiLearnCoordinator: cancelled");
}

bool MidiLearnCoordinator::isLearning(const ChainNodePath& path, int paramIndex) const {
    return armed_ && armedPath_ == path && armedParam_ == paramIndex;
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

    // Reset armed state before notifying
    armed_ = false;
    armedPath_ = {};
    armedParam_ = -1;
    armedDisplayName_ = {};

    // ---- Build target ----
    // Prefer alias if one exists in the registry for this (path, paramIndex).
    Target target;
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
        DBG("MidiLearnCoordinator: using static target");
    }

    // ---- Build source ----
    BindingSource source;
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

    BindingRegistry::getInstance().add(scope_, binding);

    DBG("MidiLearnCoordinator: binding created, scope="
        << (scope_ == BindingScope::Global ? "Global" : "Project"));

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
