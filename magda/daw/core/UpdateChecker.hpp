// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace magda {

/**
 * @brief Check GitHub for a newer MAGDA release.
 *
 * A stable build asks for the latest stable release; a pre-release build lists
 * every release, pre-releases included (#3219). The check runs on a background
 * thread, unauthenticated: 60 req/hour per IP covers one check per 24h.
 */
class UpdateChecker {
  public:
    UpdateChecker() = delete;

    struct Result {
        bool success = false;          // Network + parse succeeded
        bool updateAvailable = false;  // True when latest > current
        juce::String currentVersion;   // e.g. "1.0.0-beta.1"
        juce::String latestVersion;    // e.g. "1.0.0-beta.2"
        juce::String releaseUrl;       // html_url of the latest release
        juce::String errorMessage;     // Populated when success == false
    };

    struct Release {
        juce::String version;  ///< Tag without its leading "v"; empty when none was found.
        juce::String url;      ///< The release's html_url.
    };

    using ResultCallback = std::function<void(const Result&)>;

    /**
     * Kick off an async check. The callback fires on the message thread.
     * Safe to call multiple times — each call spawns its own thread.
     */
    static void checkAsync(ResultCallback onResult);

    /**
     * Has it been at least 24h since the last check (per Config)?
     * Used to rate-limit the automatic check on startup.
     */
    static bool shouldAutoCheck();

    /** Stamp the Config with "now" and save. Call after a successful check. */
    static void markChecked();

    /**
     * @brief Semver precedence of two versions: -1 if a < b, 0 if equal, 1 if a > b.
     *
     * 1.0.0-beta.1 < 1.0.0-beta.2 < 1.0.0. Tolerates a leading "v", build metadata
     * and a git-describe tail ("-15-g1391223"), which compares equal to its tag.
     */
    static int compareVersions(const juce::String& a, const juce::String& b);

    /** @brief True when @p version carries a pre-release suffix, ignoring a git-describe tail. */
    static bool isPreRelease(const juce::String& version);

    /** @brief The highest-versioned release in a GitHub response: one release or a list. */
    static Release newestRelease(const juce::String& json);
};

}  // namespace magda
