#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace magda {

/**
 * Check GitHub for a newer MAGDA release.
 *
 * Hits https://api.github.com/repos/Conceptual-Machines/magda-core/releases/latest
 * on a background thread, parses the tag_name, compares against the embedded
 * MAGDA_VERSION, and reports the outcome via onResult on the message thread.
 *
 * No authentication is used — the unauthenticated 60 req/hour per-IP limit is
 * plenty since each user checks at most once per 24h.
 */
class UpdateChecker {
  public:
    struct Result {
        bool success = false;          // Network + parse succeeded
        bool updateAvailable = false;  // True when latest > current
        juce::String currentVersion;   // e.g. "0.4.8"
        juce::String latestVersion;    // e.g. "0.5.0"
        juce::String releaseUrl;       // html_url of the latest release
        juce::String errorMessage;     // Populated when success == false
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
     * Compare two version strings in major.minor.patch form.
     * Returns -1 if a < b, 0 if equal, 1 if a > b.
     * Tolerates leading "v", trailing git-describe suffixes ("-15-g1391223"),
     * and RC/pre-release tails ("-rc2") which are treated as earlier than
     * the clean version.
     */
    static int compareVersions(const juce::String& a, const juce::String& b);

  private:
    UpdateChecker() = delete;
};

}  // namespace magda
