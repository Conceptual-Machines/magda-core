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
     * @brief Load profiles from bundled and user directories.
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

    /**
     * @brief Find a profile whose portMatchPattern is a substring of deviceName.
     *
     * Match is case-insensitive. Returns the first matching profile.
     * Profiles with an empty portMatchPattern never match.
     */
    std::optional<ControllerProfile> findByPortName(const juce::String& deviceName) const;

  private:
    ControllerProfileRegistry() = default;

    /** Mirror of StringTable::findLangDirectory() substituting "controllers" for "lang". */
    static juce::File findBundledControllersDirectory();

    /** ~/Library/Application Support/MAGDA/controllers (or platform equivalent). */
    static juce::File userControllersDirectory();

    /** Load all .json files from dir into profiles_, user=true means user-wins-on-collision. */
    void loadFromDirectory(const juce::File& dir, bool isUser);

    std::vector<ControllerProfile> profiles_;
};

}  // namespace magda
