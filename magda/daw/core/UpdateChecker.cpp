#include "UpdateChecker.hpp"

#include <juce_core/juce_core.h>

#include "Config.hpp"
#include "magda.hpp"

namespace magda {

namespace {

constexpr const char* kLatestReleaseUrl =
    "https://api.github.com/repos/Conceptual-Machines/magda-core/releases/latest";

constexpr int64_t kCheckIntervalMs = 24LL * 60LL * 60LL * 1000LL;  // 24 hours

/** Strip leading "v" and trailing git-describe/pre-release suffixes. */
juce::String normalizeVersion(const juce::String& raw) {
    auto s = raw.trim();
    if (s.startsWithIgnoreCase("v"))
        s = s.substring(1);
    // Drop anything after the first non-version char (- or +) so
    // "0.4.8-10-g83eb35e0" becomes "0.4.8" and "0.5.0-rc2" becomes "0.5.0".
    for (int i = 0; i < s.length(); ++i) {
        auto c = s[i];
        if (!(juce::CharacterFunctions::isDigit(c) || c == '.')) {
            s = s.substring(0, i);
            break;
        }
    }
    return s;
}

/** Parse a normalized "a.b.c" into up to three ints; missing components = 0. */
std::array<int, 3> parseSemver(const juce::String& v) {
    juce::StringArray parts;
    parts.addTokens(v, ".", {});
    std::array<int, 3> out{0, 0, 0};
    for (int i = 0; i < 3 && i < parts.size(); ++i)
        out[static_cast<size_t>(i)] = parts[i].getIntValue();
    return out;
}

juce::String extractTagFromJson(const juce::String& body) {
    auto parsed = juce::JSON::parse(body);
    if (auto* obj = parsed.getDynamicObject())
        return obj->getProperty("tag_name").toString();
    return {};
}

juce::String extractHtmlUrlFromJson(const juce::String& body) {
    auto parsed = juce::JSON::parse(body);
    if (auto* obj = parsed.getDynamicObject())
        return obj->getProperty("html_url").toString();
    return {};
}

class CheckThread : public juce::Thread {
  public:
    explicit CheckThread(UpdateChecker::ResultCallback cb)
        : juce::Thread("MAGDA UpdateChecker"), callback_(std::move(cb)) {}

    void run() override {
        UpdateChecker::Result result;
        result.currentVersion = MAGDA_VERSION;

        juce::URL url(kLatestReleaseUrl);
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

        auto body = stream->readEntireStreamAsString();
        auto tag = extractTagFromJson(body);
        if (tag.isEmpty()) {
            result.errorMessage = "Could not parse latest release tag.";
            dispatch(result);
            return;
        }

        result.success = true;
        result.latestVersion = normalizeVersion(tag);
        result.releaseUrl = extractHtmlUrlFromJson(body);
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
    auto na = parseSemver(normalizeVersion(a));
    auto nb = parseSemver(normalizeVersion(b));
    for (size_t i = 0; i < 3; ++i) {
        if (na[i] < nb[i])
            return -1;
        if (na[i] > nb[i])
            return 1;
    }
    return 0;
}

}  // namespace magda
