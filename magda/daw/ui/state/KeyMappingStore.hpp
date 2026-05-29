#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

// ============================================================================
// KEY MAPPING STORE (epic:command-registry, #20)
// ============================================================================
//
// Persists user keyboard-shortcut remaps across sessions. The defaults already
// exist (each command's getCommandInfo() calls addDefaultKeypress), and
// ApplicationCommandManager::getKeyMappings() is wired as the key listener -
// what was missing is saving and reloading the user's overrides.
//
// Storage is the opaque "keyboardBindings" blob on Config (#22): a string
// holding the XML that KeyPressMappingSet::createXml(true) produces, i.e. only
// the differences from the code-defined defaults. restoreFromXml() resets to
// defaults and re-applies those differences, so adding/removing a default in
// code stays the source of truth and old saved overrides still layer on top.
//
// Lifecycle: construct AFTER ApplicationCommandManager::registerAllCommands-
// ForTarget() (the commands and their defaults must exist), then call
// restore(). The store listens to the mapping set and re-persists on any
// change, so the editor UI (#862) and reset-to-defaults need no extra wiring.
// ============================================================================

class KeyMappingStore : public juce::ChangeListener {
  public:
    explicit KeyMappingStore(juce::ApplicationCommandManager& commandManager);
    ~KeyMappingStore() override;

    // Apply the user's saved overrides (from Config) on top of the defaults.
    // Call once, after the commands have been registered.
    void restore();

    // Drop all user overrides, returning every command to its default key, and
    // persist that (so the cleared state survives a restart).
    void resetToDefaults();

  private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void persist();

    juce::ApplicationCommandManager& commandManager_;
    // While we are the ones mutating the mapping set (restore / reset), ignore
    // the resulting change callback so we don't immediately re-persist it.
    bool suppressPersist_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyMappingStore)
};

}  // namespace magda
