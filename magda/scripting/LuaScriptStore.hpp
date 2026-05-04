#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda::scripting {

/**
 * Enumerates Lua scripts stored under the per-user MAGDA app data directory.
 *
 * Default layout:
 *   ~/Library/MAGDA/Scripts/Controllers/      (macOS — JUCE's
 *                                              userApplicationDataDirectory
 *                                              is ~/Library on macOS)
 *   %APPDATA%/MAGDA/Scripts/Controllers/      (Windows)
 *   ~/.config/MAGDA/Scripts/Controllers/      (Linux)
 *
 * Per-project storage is deferred to a follow-up; v1 is global only.
 *
 * Factory scripts shipped in resources/controllers/scripts/ are not enumerated
 * by this class — see findBundledScriptsDirectory() for that pool.
 */
class LuaScriptStore {
  public:
    /** Default constructor uses the per-user MAGDA scripts directory. */
    LuaScriptStore();

    /** Test seam: redirect to a custom directory. */
    explicit LuaScriptStore(const juce::File& root);

    /** Filesystem root the store enumerates. Created on demand by ensureExists(). */
    const juce::File& root() const noexcept {
        return root_;
    }

    /** Create the root directory if it doesn't already exist. Idempotent. */
    bool ensureExists() const;

    /** All `*.lua` files directly under root, sorted by name. */
    std::vector<juce::File> enumerate() const;

    /** Locate the bundled (factory) controller scripts directory. Returns an
     *  invalid juce::File if not found. Mirrors
     *  ControllerProfileRegistry::findBundledControllersDirectory(). */
    static juce::File findBundledScriptsDirectory();

  private:
    juce::File root_;
};

}  // namespace magda::scripting
