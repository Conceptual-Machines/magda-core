#include "LinkModeManager.hpp"

namespace magda {

namespace {

juce::String linkPathString(const ChainNodePath& path) {
    return path.isValid() ? path.toString() : juce::String("<invalid>");
}

}  // namespace

LinkModeManager& LinkModeManager::getInstance() {
    static LinkModeManager instance;
    return instance;
}

LinkModeManager::LinkModeManager() {
    // Start with no link mode active
}

// ============================================================================
// Mod Link Mode
// ============================================================================

void LinkModeManager::enterModLinkMode(const ChainNodePath& parentPath, int modIndex) {
    // Exit any existing link mode first
    if (linkModeType_ == LinkModeType::Macro) {
        exitMacroLinkMode();
    }

    modSelection_.parentPath = parentPath;
    modSelection_.modIndex = modIndex;
    linkModeType_ = LinkModeType::Mod;

    DBG("[LinkMode] enter mod parent=" << linkPathString(parentPath) << " index=" << modIndex);
    notifyModLinkModeChanged(true, modSelection_);
}

void LinkModeManager::exitModLinkMode() {
    if (linkModeType_ != LinkModeType::Mod) {
        return;
    }

    auto oldSelection = modSelection_;
    linkModeType_ = LinkModeType::None;
    modSelection_ = ModSelection{};

    DBG("[LinkMode] exit mod parent=" << linkPathString(oldSelection.parentPath)
                                      << " index=" << oldSelection.modIndex);
    notifyModLinkModeChanged(false, oldSelection);
}

void LinkModeManager::toggleModLinkMode(const ChainNodePath& parentPath, int modIndex) {
    if (isModInLinkMode(parentPath, modIndex)) {
        exitModLinkMode();
    } else {
        enterModLinkMode(parentPath, modIndex);
    }
}

bool LinkModeManager::isModInLinkMode(const ChainNodePath& parentPath, int modIndex) const {
    return linkModeType_ == LinkModeType::Mod && modSelection_.parentPath == parentPath &&
           modSelection_.modIndex == modIndex;
}

// ============================================================================
// Macro Link Mode
// ============================================================================

void LinkModeManager::enterMacroLinkMode(const ChainNodePath& parentPath, int macroIndex) {
    // Exit any existing link mode first
    if (linkModeType_ == LinkModeType::Mod) {
        exitModLinkMode();
    }

    macroSelection_.parentPath = parentPath;
    macroSelection_.macroIndex = macroIndex;
    linkModeType_ = LinkModeType::Macro;

    DBG("[LinkMode] enter macro parent=" << linkPathString(parentPath) << " index=" << macroIndex);
    notifyMacroLinkModeChanged(true, macroSelection_);
}

void LinkModeManager::exitMacroLinkMode() {
    if (linkModeType_ != LinkModeType::Macro) {
        return;
    }

    auto oldSelection = macroSelection_;
    linkModeType_ = LinkModeType::None;
    macroSelection_ = MacroSelection{};

    DBG("[LinkMode] exit macro parent=" << linkPathString(oldSelection.parentPath)
                                        << " index=" << oldSelection.macroIndex);
    notifyMacroLinkModeChanged(false, oldSelection);
}

void LinkModeManager::toggleMacroLinkMode(const ChainNodePath& parentPath, int macroIndex) {
    if (isMacroInLinkMode(parentPath, macroIndex)) {
        exitMacroLinkMode();
    } else {
        enterMacroLinkMode(parentPath, macroIndex);
    }
}

bool LinkModeManager::isMacroInLinkMode(const ChainNodePath& parentPath, int macroIndex) const {
    return linkModeType_ == LinkModeType::Macro && macroSelection_.parentPath == parentPath &&
           macroSelection_.macroIndex == macroIndex;
}

// ============================================================================
// Exit All
// ============================================================================

void LinkModeManager::exitAllLinkModes() {
    if (linkModeType_ == LinkModeType::Mod) {
        exitModLinkMode();
    } else if (linkModeType_ == LinkModeType::Macro) {
        exitMacroLinkMode();
    }
}

// ============================================================================
// Listeners
// ============================================================================

void LinkModeManager::addListener(LinkModeManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void LinkModeManager::removeListener(LinkModeManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void LinkModeManager::notifyModLinkModeChanged(bool active, const ModSelection& selection) {
    for (auto* listener : listeners_) {
        listener->modLinkModeChanged(active, selection);
    }
}

void LinkModeManager::notifyMacroLinkModeChanged(bool active, const MacroSelection& selection) {
    for (auto* listener : listeners_) {
        listener->macroLinkModeChanged(active, selection);
    }
}

}  // namespace magda
