#include "ControllerProfileRegistry.hpp"

#include <algorithm>

namespace magda {

ControllerProfileRegistry& ControllerProfileRegistry::getInstance() {
    static ControllerProfileRegistry instance;
    return instance;
}

// ============================================================================
// Path helpers
// ============================================================================

juce::File ControllerProfileRegistry::findBundledControllersDirectory() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    juce::Array<juce::File> candidates;
#if JUCE_MAC
    candidates.add(appFile.getChildFile("Contents/Resources/controllers"));
#endif
#if JUCE_LINUX
    // Under an AppImage, JUCE's currentApplicationFile returns the outer
    // .AppImage launcher path (via argv[0]/dladdr), so the directory ends up
    // looked for next to the launcher instead of inside the mount.
    // /proc/self/exe always resolves to the real binary inside the AppImage mount.
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        candidates.add(real.getParentDirectory().getChildFile("controllers"));
#endif
    // Next to the binary (Windows/Linux, and macOS portable layout).
    candidates.add(appFile.getParentDirectory().getChildFile("controllers"));

    // Dev-tree fallback: walk up looking for a resources/controllers sibling.
    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe = walk.getChildFile("resources").getChildFile("controllers");
        if (maybe.isDirectory()) {
            candidates.add(maybe);
            break;
        }
        walk = walk.getParentDirectory();
    }

    for (const auto& c : candidates)
        if (c.isDirectory())
            return c;
    return {};
}

juce::File ControllerProfileRegistry::userControllersDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("controllers");
}

// ============================================================================
// Load
// ============================================================================

void ControllerProfileRegistry::load() {
    profiles_.clear();

    auto bundledDir = findBundledControllersDirectory();
    if (bundledDir.isDirectory()) {
        loadFromDirectory(bundledDir, false);
    } else {
        DBG("ControllerProfileRegistry: bundled controllers directory not found");
    }

    auto userDir = userControllersDirectory();
    if (userDir.isDirectory()) {
        loadFromDirectory(userDir, true);
    }
    // (No log if user dir doesn't exist -- that's normal on first run)
}

void ControllerProfileRegistry::loadFromDirectory(const juce::File& dir, bool isUser) {
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.json");
    for (const auto& file : files) {
        juce::String jsonText = file.loadFileAsString();
        auto parsed = juce::JSON::parse(jsonText);
        if (parsed.isVoid()) {
            DBG("ControllerProfileRegistry: failed to parse JSON from " << file.getFullPathName());
            continue;
        }

        auto profileOpt = decodeControllerProfile(parsed);
        if (!profileOpt.has_value()) {
            DBG("ControllerProfileRegistry: decodeControllerProfile returned nullopt for "
                << file.getFullPathName());
            continue;
        }

        auto& profile = *profileOpt;

        if (isUser) {
            // User profile wins on id collision: replace existing entry.
            auto it = std::find_if(profiles_.begin(), profiles_.end(),
                                   [&](const ControllerProfile& p) { return p.id == profile.id; });
            if (it != profiles_.end()) {
                DBG("ControllerProfileRegistry: user profile '"
                    << profile.id << "' replaces bundled entry (from " << file.getFullPathName()
                    << ")");
                *it = profile;
                continue;
            }
        }

        DBG("ControllerProfileRegistry: loaded profile '" << profile.id << "' from "
                                                          << file.getFullPathName());
        profiles_.push_back(profile);
    }
}

// ============================================================================
// Queries
// ============================================================================

std::vector<ControllerProfile> ControllerProfileRegistry::all() const {
    return profiles_;
}

std::optional<ControllerProfile> ControllerProfileRegistry::findById(const juce::String& id) const {
    for (const auto& p : profiles_)
        if (p.id == id)
            return p;
    return std::nullopt;
}

}  // namespace magda
