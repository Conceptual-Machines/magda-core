#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "ControllerProfile.hpp"

namespace magda {

// ============================================================================
// ControllerProfileRegistry
// ============================================================================

/**
 * @brief Singleton read-only registry of hardware controller profiles.
 *
 * Loaded once at app startup from bundled JSON files and (optionally) from
 * user-supplied JSON files in the user data directory. No mutations after load.
 *
 * Bundled profiles live in resources/controllers/ (next to the binary or inside
 * Contents/Resources/controllers on macOS).
 * User profiles live in ~/Library/Application Support/MAGDA/controllers/
 * (or the platform equivalent). User profiles with the same id as a bundled
 * profile replace the bundled entry.
 */
class ControllerProfileRegistry {
  public:
    static ControllerProfileRegistry& getInstance();

    /**
     * @brief Load profiles from the user directory, seeding from bundled on first launch.
     *
     * If the user directory does not yet exist, bundled starter profiles are copied in
     * once. After that, deletion is durable — bundled files are never re-read at runtime.
     *
     * Safe to call multiple times (re-loads on each call, replacing existing data).
     */
    void load();

    // ========================================================================
    // Queries
    // ========================================================================

    /** Return all profiles (bundled + user). User profiles win on id collision. */
    std::vector<ControllerProfile> all() const;

    /** Find a profile by stable id string. Returns nullopt if not found. */
    std::optional<ControllerProfile> findById(const juce::String& id) const;

  private:
    ControllerProfileRegistry() = default;

    /** Mirror of StringTable::findLangDirectory() substituting "controllers" for "lang". */
    static juce::File findBundledControllersDirectory();

    /** ~/Library/Application Support/MAGDA/controllers (or platform equivalent). */
    static juce::File userControllersDirectory();

    /** Load all .json files from dir into profiles_. */
    void loadFromDirectory(const juce::File& dir);

    /** Copy every bundled *.json into userDir (creates userDir if missing). */
    void seedUserDirectory(const juce::File& userDir);

    std::vector<ControllerProfile> profiles_;
};

}  // namespace magda
