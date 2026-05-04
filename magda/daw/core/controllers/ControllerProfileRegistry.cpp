#include "ControllerProfileRegistry.hpp"

#include <algorithm>

#include "../AppPaths.hpp"

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
    candidates.add(appFile.getChildFile("Contents/Resources/controllers/profiles"));
#endif
#if JUCE_LINUX
    // Under an AppImage, JUCE's currentApplicationFile returns the outer
    // .AppImage launcher path (via argv[0]/dladdr), so the directory ends up
    // looked for next to the launcher instead of inside the mount.
    // /proc/self/exe always resolves to the real binary inside the AppImage mount.
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        candidates.add(real.getParentDirectory().getChildFile("controllers/profiles"));
#endif
    // Next to the binary (Windows/Linux, and macOS portable layout).
    candidates.add(appFile.getParentDirectory().getChildFile("controllers/profiles"));

    // Dev-tree fallback: walk up looking for a resources/controllers/profiles sibling.
    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe =
            walk.getChildFile("resources").getChildFile("controllers").getChildFile("profiles");
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
    return magda::paths::controllerProfilesDir();
}

juce::String ControllerProfileRegistry::filenameForProfileId(const juce::String& id) {
    // replaceCharacters requires the source and replacement strings to be the
    // same length — 10 chars to sanitise, 10 underscores.
    return id.replaceCharacters(" /\\:?<>|\"*", "__________") + ".json";
}

juce::File ControllerProfileRegistry::userFileForProfileId(const juce::String& id) {
    return userControllersDirectory().getChildFile(filenameForProfileId(id));
}

juce::File ControllerProfileRegistry::findSourceFileForProfileId(const juce::String& id) const {
    // Try the canonical filename in the user dir first — fast path for
    // user-imported profiles (matches load() precedence: user wins over bundled).
    auto direct = userFileForProfileId(id);
    if (direct.existsAsFile())
        return direct;

    // Walk both pools and pick the file whose JSON body advertises this id.
    // Bundled profiles use shipping filenames that don't always match the id
    // (e.g. id "novation.launchkey_mini_mk4" lives in
    // novation_launchkey_mini_mk4.json), so we have to read each file.
    auto matchInDir = [&id](const juce::File& dir) -> juce::File {
        if (!dir.isDirectory())
            return {};
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.json")) {
            auto parsed = juce::JSON::parse(f.loadFileAsString());
            if (parsed.isVoid())
                continue;
            auto profileOpt = decodeControllerProfile(parsed);
            if (profileOpt.has_value() && profileOpt->id == id)
                return f;
        }
        return {};
    };

    if (auto user = matchInDir(userControllersDirectory()); user.existsAsFile())
        return user;
    return matchInDir(findBundledControllersDirectory());
}

// ============================================================================
// Load
// ============================================================================

void ControllerProfileRegistry::load() {
    profiles_.clear();

    // Bundled (factory) pool first; user pool second so user entries override
    // bundled entries with the same id.
    if (auto bundledDir = findBundledControllersDirectory(); bundledDir.isDirectory())
        loadFromDirectory(bundledDir);

    if (auto userDir = userControllersDirectory(); userDir.isDirectory())
        loadFromDirectory(userDir);
}

void ControllerProfileRegistry::loadFromDirectory(const juce::File& dir) {
    // Last-wins on id collision — two files with the same "id" (easy to
    // produce by copying a JSON) otherwise make findById non-deterministic.
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

        auto existing =
            std::find_if(profiles_.begin(), profiles_.end(),
                         [&id = profileOpt->id](const ControllerProfile& p) { return p.id == id; });
        if (existing != profiles_.end()) {
            DBG("ControllerProfileRegistry: duplicate id '" << profileOpt->id << "' in "
                                                            << file.getFullPathName()
                                                            << " — replacing earlier entry");
            *existing = *profileOpt;
        } else {
            DBG("ControllerProfileRegistry: loaded profile '" << profileOpt->id << "' from "
                                                              << file.getFullPathName());
            profiles_.push_back(*profileOpt);
        }
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
