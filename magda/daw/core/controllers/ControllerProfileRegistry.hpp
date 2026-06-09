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
 * @brief Singleton registry of hardware controller profiles.
 *
 * Profiles come from two pools:
 *   - Bundled (factory): shipped in resources/controllers/profiles/, copied
 *     into the app on build. Read-only at runtime — never written to.
 *   - User: ~/Library/Application Support/MAGDA/controllers/ (or platform
 *     equivalent), populated by the in-app Import flow.
 *
 * load() reads bundled first, then user. User entries override bundled entries
 * with the same "id" so users can shadow a factory profile by importing a
 * customised copy. Within a single pool, two files sharing an "id" resolve
 * last-wins.
 *
 * All mutation happens by writing new JSON files to the user directory and
 * calling load() again.
 */
class ControllerProfileRegistry {
  public:
    static ControllerProfileRegistry& getInstance();

    /**
     * @brief Load profiles from the bundled and user directories.
     *
     * Bundled profiles load first; user profiles load second and override
     * bundled entries with the same id.
     *
     * Safe to call multiple times (re-loads on each call, replacing existing data).
     */
    void load();

    // ========================================================================
    // Queries
    // ========================================================================

    /** Return all profiles loaded from the user directory. */
    std::vector<ControllerProfile> all() const;

    /** Find a profile by stable id string. Returns nullopt if not found. */
    std::optional<ControllerProfile> findById(const juce::String& id) const;

    /** ~/Library/Application Support/MAGDA/controllers (or platform equivalent). */
    static juce::File userControllersDirectory();

    /** Derive the on-disk filename for a profile id (sanitised + ".json"). */
    static juce::String filenameForProfileId(const juce::String& id);

    /** Full path to a profile's JSON file in the user directory. */
    static juce::File userFileForProfileId(const juce::String& id);

    /**
     * Look up the on-disk file a profile was loaded from. Bundled files don't
     * have to be named after their id (the loader reads the id out of the JSON
     * body), so userFileForProfileId() can synthesise a path that doesn't
     * exist. This walks the user directory and returns the file whose parsed
     * id matches. Returns an invalid juce::File when no match.
     */
    juce::File findSourceFileForProfileId(const juce::String& id) const;

  private:
    ControllerProfileRegistry() = default;

    /** Locate the bundled "controllers/profiles" directory.
     *  Mirror of StringTable::findLangDirectory() substituting "controllers/profiles" for "lang". */
    static juce::File findBundledControllersDirectory();

    /** Load all .json files from dir into profiles_; later entries override
     *  earlier ones with the same id. */
    void loadFromDirectory(const juce::File& dir);

    std::vector<ControllerProfile> profiles_;
};

}  // namespace magda
