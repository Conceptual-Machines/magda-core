#include "magda/scripting/LuaScriptStore.hpp"

#include <algorithm>

#include "magda/daw/core/AppPaths.hpp"

namespace magda::scripting {

LuaScriptStore::LuaScriptStore() : root_(magda::paths::controllerScriptsDir()) {}

LuaScriptStore::LuaScriptStore(const juce::File& root) : root_(root) {}

bool LuaScriptStore::ensureExists() const {
    if (root_.exists())
        return root_.isDirectory();
    auto result = root_.createDirectory();
    return result.wasOk();
}

std::vector<juce::File> LuaScriptStore::enumerate() const {
    std::vector<juce::File> out;
    if (!root_.isDirectory())
        return out;

    auto found = root_.findChildFiles(juce::File::findFiles, /*searchRecursively*/ false, "*.lua");
    out.reserve(static_cast<size_t>(found.size()));
    for (auto& f : found)
        out.push_back(f);

    std::sort(out.begin(), out.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });

    return out;
}

juce::File LuaScriptStore::findBundledScriptsDirectory() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    juce::Array<juce::File> candidates;
#if JUCE_MAC
    candidates.add(appFile.getChildFile("Contents/Resources/controllers/scripts"));
#endif
#if JUCE_LINUX
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        candidates.add(real.getParentDirectory().getChildFile("controllers/scripts"));
#endif
    candidates.add(appFile.getParentDirectory().getChildFile("controllers/scripts"));

    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe =
            walk.getChildFile("resources").getChildFile("controllers").getChildFile("scripts");
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

}  // namespace magda::scripting
