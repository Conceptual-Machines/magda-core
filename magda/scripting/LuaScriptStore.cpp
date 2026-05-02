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

}  // namespace magda::scripting
