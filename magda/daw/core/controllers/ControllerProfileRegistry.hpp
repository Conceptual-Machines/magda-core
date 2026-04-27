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
 * At runtime the registry reads only from the user directory
 * (~/Library/Application Support/MAGDA/controllers/ on macOS, or the platform
 * equivalent). On every load the bundled JSON files under resources/controllers/
 * are copied in additively: any bundled file whose filename is missing from the
 * user directory is seeded; existing files are never overwritten, so user edits
 * and deletions stick.
 *
 * Two files in the user directory that share an "id" field resolve last-wins:
 * the later file overrides the earlier entry so findById stays deterministic.
 *
 * All mutation happens by writing new JSON files to the user directory and
 * calling load() again.
 */
class ControllerProfileRegistry {
  public:
    static ControllerProfileRegistry& getInstance();

    /**
     * @brief Load profiles from the user directory, seeding any missing bundled files.
     *
     * Bundled files whose filename is absent from the user directory are copied in.
     * Existing user files (including bundled files the user has edited) are left
     * untouched, so deletions and customisations are durable.
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

    /** Mirror of StringTable::findLangDirectory() substituting "controllers" for "lang". */
    static juce::File findBundledControllersDirectory();

    /** Load all .json files from dir into profiles_. */
    void loadFromDirectory(const juce::File& dir);

    /** Copy every bundled *.json into userDir (creates userDir if missing). */
    void seedUserDirectory(const juce::File& userDir);

    std::vector<ControllerProfile> profiles_;
};

}  // namespace magda
