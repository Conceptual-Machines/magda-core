// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateChecker.hpp"

#include <juce_core/juce_core.h>

#include <array>
#include <regex>

#include "Config.hpp"
#include "magda.hpp"

namespace magda {

namespace {

constexpr const char* kLatestReleaseUrl =
    "https://api.github.com/repos/Conceptual-Machines/magda-core/releases/latest";
constexpr const char* kAllReleasesUrl =
    "https://api.github.com/repos/Conceptual-Machines/magda-core/releases";

constexpr int64_t kCheckIntervalMs = 24LL * 60LL * 60LL * 1000LL;  // 24 hours

struct ParsedVersion {
    std::array<int, 3> core{0, 0, 0};
    juce::StringArray preRelease;  ///< Dot-separated identifiers; empty for a release.
};

juce::String withoutLeadingV(const juce::String& raw) {
    auto s = raw.trim();
    return s.startsWithIgnoreCase("v") ? s.substring(1) : s;
}

/** @brief Parse "v1.2.3-beta.1+build"; a git-describe tail belongs to the tag before it. */
ParsedVersion parseVersion(const juce::String& raw) {
    auto s = withoutLeadingV(raw).upToFirstOccurrenceOf("+", false, false);
    s = juce::String(std::regex_replace(s.toStdString(), std::regex(R"(-\d+-g[0-9a-f]+$)"), ""));

    ParsedVersion out;
    juce::StringArray parts;
    parts.addTokens(s.upToFirstOccurrenceOf("-", false, false), ".", {});
    for (int i = 0; i < 3 && i < parts.size(); ++i)
        out.core[static_cast<size_t>(i)] = parts[i].getIntValue();

    if (s.containsChar('-'))
        out.preRelease.addTokens(s.fromFirstOccurrenceOf("-", false, false), ".", {});
    return out;
}

template <typename T> int compareValues(T a, T b) {
    return a < b ? -1 : (b < a ? 1 : 0);
}

/**
 * @brief Semver order for one pre-release identifier: numeric ones sort first.
 *
 * Alphanumeric identifiers compare naturally so rc2 < rc10, where strict semver
 * puts rc10 first; MAGDA's older tags are rc0, rc1, rc2.
 */
int compareIdentifiers(const juce::String& a, const juce::String& b) {
    const bool aNumeric = a.containsOnly("0123456789");
    const bool bNumeric = b.containsOnly("0123456789");
    if (aNumeric && bNumeric)
        return compareValues(a.getLargeIntValue(), b.getLargeIntValue());
    if (aNumeric != bNumeric)
        return aNumeric ? -1 : 1;
    return compareValues(a.compareNatural(b, true), 0);
}

class CheckThread : public juce::Thread {
  public:
    explicit CheckThread(UpdateChecker::ResultCallback cb)
        : juce::Thread("MAGDA UpdateChecker"), callback_(std::move(cb)) {}

    void run() override {
        UpdateChecker::Result result;
        result.currentVersion = MAGDA_VERSION;

        juce::URL url(UpdateChecker::isPreRelease(result.currentVersion) ? kAllReleasesUrl
                                                                         : kLatestReleaseUrl);
        juce::StringPairArray responseHeaders;
        int statusCode = 0;

        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs(10000)
                           .withExtraHeaders(juce::String("Accept: application/vnd.github+json\r\n"
                                                          "User-Agent: MAGDA/") +
                                             MAGDA_VERSION)
                           .withResponseHeaders(&responseHeaders)
                           .withStatusCode(&statusCode);

        auto stream = url.createInputStream(options);
        if (stream == nullptr) {
            result.errorMessage = "Network error (no response).";
            dispatch(result);
            return;
        }

        if (statusCode < 200 || statusCode >= 300) {
            result.errorMessage = "GitHub returned HTTP " + juce::String(statusCode) + ".";
            dispatch(result);
            return;
        }

        auto newest = UpdateChecker::newestRelease(stream->readEntireStreamAsString());
        if (newest.version.isEmpty()) {
            result.errorMessage = "Could not parse latest release tag.";
            dispatch(result);
            return;
        }

        result.success = true;
        result.latestVersion = newest.version;
        result.releaseUrl = newest.url;
        result.updateAvailable =
            UpdateChecker::compareVersions(result.currentVersion, result.latestVersion) < 0;
        dispatch(result);
    }

  private:
    void dispatch(const UpdateChecker::Result& r) {
        // Hop to the message thread before invoking the callback.
        juce::MessageManager::callAsync([cb = callback_, r]() {
            if (cb)
                cb(r);
        });
    }

    UpdateChecker::ResultCallback callback_;
};

// Keep active threads alive until they finish. JUCE requires the Thread
// object to outlive the OS thread; we detach ownership into a static list
// and prune completed entries opportunistically.
juce::CriticalSection g_threadsLock;
std::vector<std::unique_ptr<CheckThread>> g_activeThreads;

void pruneFinishedThreads() {
    const juce::ScopedLock sl(g_threadsLock);
    g_activeThreads.erase(
        std::remove_if(g_activeThreads.begin(), g_activeThreads.end(),
                       [](const std::unique_ptr<CheckThread>& t) { return !t->isThreadRunning(); }),
        g_activeThreads.end());
}

}  // namespace

void UpdateChecker::checkAsync(ResultCallback onResult) {
    pruneFinishedThreads();
    auto thread = std::make_unique<CheckThread>(std::move(onResult));
    thread->startThread();
    const juce::ScopedLock sl(g_threadsLock);
    g_activeThreads.push_back(std::move(thread));
}

bool UpdateChecker::shouldAutoCheck() {
    auto& cfg = Config::getInstance();
    if (!cfg.getAutoCheckUpdates())
        return false;
    auto last = cfg.getLastUpdateCheckTimestamp();
    auto now = juce::Time::getCurrentTime().toMilliseconds();
    return (now - last) >= kCheckIntervalMs;
}

void UpdateChecker::markChecked() {
    auto& cfg = Config::getInstance();
    cfg.setLastUpdateCheckTimestamp(juce::Time::getCurrentTime().toMilliseconds());
    cfg.save();
}

int UpdateChecker::compareVersions(const juce::String& a, const juce::String& b) {
    auto va = parseVersion(a);
    auto vb = parseVersion(b);
    for (size_t i = 0; i < 3; ++i)
        if (va.core[i] != vb.core[i])
            return compareValues(va.core[i], vb.core[i]);

    const auto& pa = va.preRelease;
    const auto& pb = vb.preRelease;
    if (pa.isEmpty() != pb.isEmpty())
        return pa.isEmpty() ? 1 : -1;
    for (int i = 0; i < juce::jmin(pa.size(), pb.size()); ++i)
        if (auto order = compareIdentifiers(pa[i], pb[i]); order != 0)
            return order;
    return compareValues(pa.size(), pb.size());
}

bool UpdateChecker::isPreRelease(const juce::String& version) {
    return !parseVersion(version).preRelease.isEmpty();
}

UpdateChecker::Release UpdateChecker::newestRelease(const juce::String& json) {
    auto parsed = juce::JSON::parse(json);
    juce::Array<juce::var> releases;
    if (auto* list = parsed.getArray())
        releases = *list;
    else
        releases.add(parsed);

    Release newest;
    for (const auto& release : releases) {
        auto* obj = release.getDynamicObject();
        if (obj == nullptr)
            continue;
        auto version = withoutLeadingV(obj->getProperty("tag_name").toString());
        if (version.isNotEmpty() &&
            (newest.version.isEmpty() || compareVersions(newest.version, version) < 0))
            newest = {version, obj->getProperty("html_url").toString()};
    }
    return newest;
}

}  // namespace magda
